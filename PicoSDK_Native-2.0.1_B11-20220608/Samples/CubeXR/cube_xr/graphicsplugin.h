#pragma once
#include "pxr/PxrApi.h"

struct Cube {
    PxrPosef    Pose;
    PxrVector3f Scale;
};

typedef struct PxrFovf {
    float    angleLeft;
    float    angleRight;
    float    angleUp;
    float    angleDown;
} PxrFovf;

typedef struct PxrProjectionView_ {
    PxrPosef                    pose;
    PxrFovf                     fov;
    PxrRecti                    imageRect;
} PxrProjectionView;

struct IGraphicsPlugin {
    virtual ~IGraphicsPlugin() = default;

    virtual void InitializeDevice() = 0;

    virtual void RenderView_N(PxrEyeType eye,const PxrProjectionView*  layerViews, uint64_t colorTexture,
                             const std::vector<Cube>& cubes,int samples) = 0;
};

// Create a opengles graphics plugin.
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_OpenGLES();
