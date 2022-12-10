#include "common.h"
#include "geometry.h"
#include "graphicsplugin.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

namespace {
constexpr float DarkSlateGray[] = {0.184313729f, 0.309803933f, 0.309803933f, 1.0f};

static const char* VertexShaderGlsl = R"_(
    #version 320 es

    in vec3 VertexPos;
    in vec3 VertexColor;

    out vec3 PSVertexColor;

    uniform mat4 ModelViewProjection;

    void main() {
       gl_Position = ModelViewProjection * vec4(VertexPos, 1.0);
       PSVertexColor = VertexColor;
    }
    )_";

static const char* FragmentShaderGlsl = R"_(
    #version 320 es

    in lowp vec3 PSVertexColor;
    out lowp vec4 FragColor;

    void main() {
       FragColor = vec4(PSVertexColor, 1);
    }
    )_";

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin(){};

    OpenGLESGraphicsPlugin(const OpenGLESGraphicsPlugin&) = delete;
    OpenGLESGraphicsPlugin& operator=(const OpenGLESGraphicsPlugin&) = delete;
    OpenGLESGraphicsPlugin(OpenGLESGraphicsPlugin&&) = delete;
    OpenGLESGraphicsPlugin& operator=(OpenGLESGraphicsPlugin&&) = delete;

    ~OpenGLESGraphicsPlugin() override {
        if (m_swapchainFramebuffer != 0) {
            glDeleteFramebuffers(1, &m_swapchainFramebuffer);
        }
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
        }
        if (m_cubeVertexBuffer != 0) {
            glDeleteBuffers(1, &m_cubeVertexBuffer);
        }
        if (m_cubeIndexBuffer != 0) {
            glDeleteBuffers(1, &m_cubeIndexBuffer);
        }

        for (auto& colorToDepth : m_colorToDepthMap) {
            if (colorToDepth.second != 0) {
                glDeleteTextures(1, &colorToDepth.second);
            }
        }
    }

    bool InitContext(){

        EGLint numConfigs;
        EGLConfig config = nullptr;
        EGLContext context;
        EGLSurface tinySurface;
        EGLSurface mainSurface;

        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(display, nullptr, nullptr);
        const int MAX_CONFIGS = 1024;
        EGLConfig configs[MAX_CONFIGS];
        eglGetConfigs(display, configs, MAX_CONFIGS, &numConfigs);
        const EGLint configAttribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
                // EGL_STENCIL_SIZE,	0,
                                        EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES,
                                        0, EGL_NONE};

        config = 0;
        for (int i = 0; i < numConfigs; i++) {
            EGLint value = 0;

            eglGetConfigAttrib(display, configs[i], EGL_RENDERABLE_TYPE, &value);
            if ((value & EGL_OPENGL_ES3_BIT) != EGL_OPENGL_ES3_BIT) {
                continue;
            }

            // Without EGL_KHR_surfaceless_context, the config needs to support both pbuffers and window surfaces.
            eglGetConfigAttrib(display, configs[i], EGL_SURFACE_TYPE, &value);
            if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
                continue;
            }

            int j = 0;
            for (; configAttribs[j] != EGL_NONE; j += 2) {
                eglGetConfigAttrib(display, configs[i], configAttribs[j], &value);
                if (value != configAttribs[j + 1]) {
                    break;
                }
            }
            if (configAttribs[j] == EGL_NONE) {
                config = configs[i];
                break;
            }
        }
        if (config == 0) {
            Log::Write(Log::Level::Error, "Failed to find EGLConfig");
            return false;
        }
        EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE, EGL_NONE, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
        if (context == EGL_NO_CONTEXT) {
            Log::Write(Log::Level::Error, "eglCreateContext() failed");
            return false;
        }
        const EGLint surfaceAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        tinySurface = eglCreatePbufferSurface(display, config, surfaceAttribs);
        if (tinySurface == EGL_NO_SURFACE) {
            Log::Write(Log::Level::Error, "eglCreatePbufferSurface() failed");
            eglDestroyContext(display, context);
            context = EGL_NO_CONTEXT;
            return false;
        }
        mainSurface = tinySurface;
        eglMakeCurrent(display, mainSurface, mainSurface, context);
        return true;
    }

    void InitializeDevice() override {
        InitContext();
        InitializeResources();
    }

    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC glFramebufferTexture2DMultisampleEXT = NULL;
    void InitializeResources() {
        glFramebufferTexture2DMultisampleEXT = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC) eglGetProcAddress(
                    "glFramebufferTexture2DMultisampleEXT");
        if (!glFramebufferTexture2DMultisampleEXT) {
            Log::Write(Log::Level::Error, "Couldn't get function pointer to glFramebufferTexture2DMultisampleEXT()!");
            return;
        }
        glGenFramebuffers(1, &m_swapchainFramebuffer);

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &VertexShaderGlsl, nullptr);
        glCompileShader(vertexShader);
        CheckShader(vertexShader);
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &FragmentShaderGlsl, nullptr);
        glCompileShader(fragmentShader);
        CheckShader(fragmentShader);
        m_program = glCreateProgram();
        glAttachShader(m_program, vertexShader);
        glAttachShader(m_program, fragmentShader);
        glLinkProgram(m_program);
        CheckProgram(m_program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        m_modelViewProjectionUniformLocation = glGetUniformLocation(m_program,
                                                                    "ModelViewProjection");

        m_vertexAttribCoords = glGetAttribLocation(m_program, "VertexPos");
        m_vertexAttribColor = glGetAttribLocation(m_program, "VertexColor");
        glGenBuffers(1, &m_cubeVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_cubeVertices), Geometry::c_cubeVertices, GL_STATIC_DRAW);

        glGenBuffers(1, &m_cubeIndexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_cubeIndices), Geometry::c_cubeIndices, GL_STATIC_DRAW);
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glEnableVertexAttribArray(m_vertexAttribCoords);
        glEnableVertexAttribArray(m_vertexAttribColor);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glVertexAttribPointer(m_vertexAttribCoords, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex), nullptr);
        glVertexAttribPointer(m_vertexAttribColor, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex),
                              reinterpret_cast<const void*>(sizeof(PxrVector3f)));
    }

    void CheckShader(GLuint shader) {
        GLint r = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096] = {};
            GLsizei length;
            glGetShaderInfoLog(shader, sizeof(msg), &length, msg);
            Log::Write(Log::Level::Error, Fmt("Compile shader failed: %s", msg));
        }
    }

    void CheckProgram(GLuint prog) {
        GLint r = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096] = {};
            GLsizei length;
            glGetProgramInfoLog(prog, sizeof(msg), &length, msg);
            Log::Write(Log::Level::Error, Fmt("Link program failed: %s", msg));
        }
    }


    uint32_t GetDepthTexture(uint32_t colorTexture) {
        // If a depth-stencil view has already been created for this back-buffer, use it.
        auto depthBufferIt = m_colorToDepthMap.find(colorTexture);
        if (depthBufferIt != m_colorToDepthMap.end()) {
            return depthBufferIt->second;
        }

        GLint width;
        GLint height;
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

        uint32_t depthTexture;
        glGenTextures(1, &depthTexture);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        m_colorToDepthMap.insert(std::make_pair(colorTexture, depthTexture));

        return depthTexture;
    }

    void RenderView_N(PxrEyeType eye,const PxrProjectionView* layerViews, uint64_t colorTexture,
                    const std::vector<Cube>& cubes, int samples) override {
        glBindFramebuffer(GL_FRAMEBUFFER, m_swapchainFramebuffer);
        if(eye == PXR_EYE_BOTH)
            eye = PXR_EYE_LEFT;

        glViewport(static_cast<GLint>(layerViews[eye].imageRect.x),
                   static_cast<GLint>(layerViews[eye].imageRect.y),
                   static_cast<GLsizei>(layerViews[eye].imageRect.width),
                   static_cast<GLsizei>(layerViews[eye].imageRect.height));

        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        const uint32_t depthTexture  = GetDepthTexture((uint32_t)colorTexture);

        if (samples > 1) {
            glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                 GL_TEXTURE_2D, (uint32_t)colorTexture, 0, samples);
            glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                 GL_TEXTURE_2D, depthTexture, 0, samples);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   (uint32_t)colorTexture, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   depthTexture, 0);
        }

        // Clear color and depth buffer.
        glClearColor(DarkSlateGray[0], DarkSlateGray[1], DarkSlateGray[2], DarkSlateGray[3]);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // Set shaders and uniform variables.
        glUseProgram(m_program);


        const auto &pose = layerViews[eye].pose;


        float nearZ = 0.05f;
        float farZ  = 100.0f;
        float left  = nearZ * tanf(layerViews[eye].fov.angleLeft);
        float right = nearZ * tanf(layerViews[eye].fov.angleRight);
        float up    = nearZ * tanf(layerViews[eye].fov.angleUp);
        float down  = nearZ * tanf(layerViews[eye].fov.angleDown);

        glm::mat4    mProjectionMatrix  = glm::mat4(1.0f);
        glm::mat4    mToViewMatrix      = glm::mat4(1.0f);
        glm::mat4    mScaleMatrix       = glm::mat4(1.0f);
        glm::quat    mQuat              = glm::quat(pose.orientation.w, pose.orientation.x,pose.orientation.y,pose.orientation.z);
        glm::mat4    mRotationMatrix    = glm::mat4(1.0f);
        glm::mat4    mTranslationMatrix = glm::mat4(1.0f);
        glm::mat4    mViewMatrix        = glm::mat4(1.0f);
        glm::mat4    mViewProjMatrix    = glm::mat4(1.0f);

        mProjectionMatrix  = glm::frustum(left, right, down, up, nearZ, farZ);
        mScaleMatrix       = glm::scale(mScaleMatrix, glm::vec3(1.0, 1.0, 1.0));
        mRotationMatrix    = glm::toMat4(mQuat);
        mTranslationMatrix = glm::translate(mTranslationMatrix, glm::vec3(pose.position.x, pose.position.y, pose.position.z));
        mToViewMatrix      = mTranslationMatrix * mRotationMatrix * mScaleMatrix;
        mViewMatrix        = glm::inverse(mToViewMatrix);
        mViewProjMatrix    = mProjectionMatrix * mViewMatrix;

        // Set cube primitive data.
        glBindVertexArray(m_vao);

        // Render each cube
        glm::mat4    modelMatrix      = glm::mat4(1.0f);
        glm::mat4    mvpMatrix        = glm::mat4(1.0f);
        for (const Cube& cube : cubes) {
            // Compute the model-view-projection transform and set it..

            modelMatrix =  glm::translate(glm::mat4(1.0f), glm::vec3(cube.Pose.position.x, cube.Pose.position.y, cube.Pose.position.z))        \
                           * glm::toMat4(glm::quat(cube.Pose.orientation.w, cube.Pose.orientation.x,cube.Pose.orientation.y,cube.Pose.orientation.z)) \
                           * glm::scale(glm::mat4(1.0f), glm::vec3(cube.Scale.x, cube.Scale.y, cube.Scale.z));
            mvpMatrix   = mViewProjMatrix * modelMatrix;

            glUniformMatrix4fv(m_modelViewProjectionUniformLocation, 1, GL_FALSE,
                               reinterpret_cast<const GLfloat *>(&mvpMatrix ));

            // Draw the cube.
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_cubeIndices)), GL_UNSIGNED_SHORT, nullptr);

        }

        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFlush();
    }

private:
    GLuint m_swapchainFramebuffer{0};
    GLuint m_program{0};
    GLint m_modelViewProjectionUniformLocation{0};
    GLint m_mvpMtx{0};
    GLint m_vertexAttribCoords{0};
    GLint m_vertexAttribColor{0};
    GLuint m_vao{0};
    GLuint m_cubeVertexBuffer{0};
    GLuint m_cubeIndexBuffer{0};
    GLenum err = -1;
    // Map color buffer to associated depth buffer. This map is populated on demand.
    std::map<uint32_t, uint32_t> m_colorToDepthMap;
};
}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_OpenGLES() {
    return std::make_shared<OpenGLESGraphicsPlugin>();
}

