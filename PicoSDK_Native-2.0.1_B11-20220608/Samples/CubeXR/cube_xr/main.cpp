#include "common.h"
#include "graphicsplugin.h"
#include "pxr/PxrApi.h"
#include "pxr/PxrInput.h"
#include <GLES3/gl3.h>

const int SAMPLE_COUNT = 4;
const int UNIT_CUBE_COUNT = 5;
const int MaxEventCount = 20;
struct AndroidAppState {
    ANativeWindow* nativeWindow = nullptr;
    bool resumed = false;

    int recommendW;
    int recommendH;
    int eyeLayerId = 0;
    uint64_t layerImages[PXR_EYE_MAX][3] = {0};

    int handCount  = 0;
    bool handState[PXR_CONTROLLER_COUNT];
    float handScale[PXR_CONTROLLER_COUNT] = {1.0};
    PxrVector2f joystick[PXR_CONTROLLER_COUNT];
    uint32_t mainController;
    PxrEventDataBuffer* eventDataPointer[MaxEventCount];
};

/**
 * Process the next main command.
 */
static void app_handle_cmd(struct android_app* app, int32_t cmd) {
    AndroidAppState* appState = (AndroidAppState*)app->userData;

    switch (cmd) {
        // There is no APP_CMD_CREATE. The ANativeActivity creates the
        // application thread from onCreate(). The application thread
        // then calls android_main().
        case APP_CMD_START: {
            Log::Write(Log::Level::Info, "    APP_CMD_START");
            Log::Write(Log::Level::Info, "onStart()");
            break;
        }
        case APP_CMD_RESUME: {
            Log::Write(Log::Level::Info, "onResume()");
            Log::Write(Log::Level::Info, "    APP_CMD_RESUME");
            //Pxr_SetInputEventCallback(true);
            appState->resumed = true;
            break;
        }
        case APP_CMD_PAUSE: {
            Log::Write(Log::Level::Info, "onPause()");
            Log::Write(Log::Level::Info, "    APP_CMD_PAUSE");
            //Pxr_SetInputEventCallback(false);
            appState->resumed = false;
            break;
        }
        case APP_CMD_STOP: {
            Log::Write(Log::Level::Info, "onStop()");
            Log::Write(Log::Level::Info, "    APP_CMD_STOP");
            break;
        }
        case APP_CMD_DESTROY: {
            Log::Write(Log::Level::Info, "onDestroy()");
            Log::Write(Log::Level::Info, "    APP_CMD_DESTROY");
            appState->nativeWindow = NULL;
            break;
        }
        case APP_CMD_INIT_WINDOW: {
            Log::Write(Log::Level::Info, "surfaceCreated()");
            Log::Write(Log::Level::Info, "    APP_CMD_INIT_WINDOW");
            appState->nativeWindow = app->window;
            break;
        }
        case APP_CMD_TERM_WINDOW: {
            Log::Write(Log::Level::Info, "surfaceDestroyed()");
            Log::Write(Log::Level::Info, "    APP_CMD_TERM_WINDOW");
            appState->nativeWindow = NULL;
            break;
        }
    }
}

static void pxrapi_init_common(struct android_app* app)
{
    PxrInitParamData initParamData = {};

    initParamData.activity      = app->activity->clazz;
    initParamData.vm            = app->activity->vm;
    initParamData.controllerdof = 1;
    initParamData.headdof       = 1;
    Pxr_SetInitializeData(&initParamData);
    Pxr_Initialize();
}

static void pxrapi_init_layers(struct android_app* app)
{
    AndroidAppState* s = (AndroidAppState*)app->userData;

    uint32_t maxW,maxH,recommendW,recommendH;
    int layerId = s->eyeLayerId;
    Pxr_GetConfigViewsInfos(&maxW,&maxH,&recommendW,&recommendH);

    s->recommendW = recommendW;
    s->recommendH = recommendH;

    PxrLayerParam layerParam = {};
    layerParam.layerId = layerId;
    layerParam.layerShape = PXR_LAYER_PROJECTION;
    layerParam.layerLayout = PXR_LAYER_LAYOUT_STEREO;
    layerParam.width = recommendW;
    layerParam.height = recommendH;
    layerParam.faceCount = 1;
    layerParam.mipmapCount = 1;
    layerParam.sampleCount = 1;
    layerParam.arraySize = 1;
    layerParam.format = GL_RGBA8;
    Pxr_CreateLayer(&layerParam);

    int eyeCounts = 2;
    for(int i = 0; i < eyeCounts; i++)
    {
        uint32_t imageCounts = 0;
        Pxr_GetLayerImageCount(layerId, (PxrEyeType)i, &imageCounts);
        for(int j = 0; j < imageCounts; j++)
        {
            Pxr_GetLayerImage(layerId, (PxrEyeType)i, j, &s->layerImages[i][j]);
        }
    }
}

static void pxrapi_init_controller(struct android_app* app)
{
    AndroidAppState* s = (AndroidAppState*)app->userData;
    Pxr_GetControllerMainInputHandle(&s->mainController);
}

static void pxrapi_init_events(struct android_app* app)
{
    AndroidAppState* s = (AndroidAppState*)app->userData;
    for(int i = 0; i<MaxEventCount; i++){
        s->eventDataPointer[i] = new PxrEventDataBuffer;
    }
}

static void pxrapi_init(struct android_app* app){
    // init pxr
    pxrapi_init_common(app);
    // create eye layer
    pxrapi_init_layers(app);
    // controller
    pxrapi_init_controller(app);
    pxrapi_init_events(app);
}

static void pxrapi_deinit(struct android_app* app) {
    AndroidAppState* s = (AndroidAppState*)app->userData;
    //destroy eye layer
    Pxr_DestroyLayer(s->eyeLayerId);
    for(int i = 0; i<MaxEventCount; i++){
        delete s->eventDataPointer[i];
    }
    Pxr_Shutdown();
}

static void dispatch_events(struct android_app* app)
{
    int eventCount = 0;
    AndroidAppState* s = (AndroidAppState*)app->userData;

    if( Pxr_PollEvent(MaxEventCount, &eventCount, s->eventDataPointer) ){
        for(int i=0; i<eventCount; i++){
            if(s->eventDataPointer[i]->type == PXR_TYPE_EVENT_DATA_SESSION_STATE_READY){
                Pxr_BeginXr();
            }else if(s->eventDataPointer[i]->type == PXR_TYPE_EVENT_DATA_SESSION_STATE_STOPPING){
                Pxr_EndXr();
            }
        }
    }

    if(Pxr_IsRunning())
    {
        uint8_t connectStatus = -1;
        PxrControllerCapability cap;

        int handCount = 0;
        for (auto hand : {PXR_CONTROLLER_LEFT, PXR_CONTROLLER_RIGHT}) {
            Pxr_GetControllerCapabilities(hand, &cap);
            if (Pxr_GetControllerConnectStatus(hand) == 1) {
                int triggerValue;
                int AXValue;
                int BYValue;
                int sideValue;
                float scale = 0.1f;
                PxrControllerInputState state;

                Pxr_GetControllerInputState(hand, &state);
                triggerValue = state.triggerValue;
                AXValue      = state.AXValue;
                BYValue      = state.BYValue;
                sideValue    = state.sideValue;

                float trigger = (float)triggerValue;    // trigger value
                if(trigger > 0.01f ){
                    Pxr_SetControllerVibration(hand, trigger, 20);
                }

                if(AXValue) {  // AX button
                    scale = 0.05f;
                }

                if(BYValue) {    // BY button
                    scale = 0.15f;
                }

                if(sideValue) {   // Side button
                    scale = 0.25f;
                }

                if(state.backValue){
                    ANativeActivity_finish(app->activity);
                }

                s->joystick[hand]  = state.Joystick;
                s->handScale[hand] = scale;
                s->handState[hand] = true;
                handCount++;
            } else {
                s->handState[hand] = false;
            }
        }
        s->handCount = handCount;
    }
}

std::vector<Cube> cubes;
std::shared_ptr<IGraphicsPlugin> graphicsPlugin;
static void init_scene(struct android_app* app)
{
    graphicsPlugin = CreateGraphicsPlugin_OpenGLES();
    graphicsPlugin->InitializeDevice();

    for(int z=-UNIT_CUBE_COUNT;z<UNIT_CUBE_COUNT;z++)
        for(int y=-UNIT_CUBE_COUNT;y<UNIT_CUBE_COUNT;y++)
            for(int x=-UNIT_CUBE_COUNT;x<UNIT_CUBE_COUNT;x++)
                cubes.push_back(Cube{ {{0.0f,0.0f,0.0f,1.0f},{0.0f+x+0.3f,0.0f+y+0.3f,0.0f+z+0.3f}}, {0.3f, 0.3f, 0.3f}});
}

static void render_frame(struct android_app* app)
{
    if(!Pxr_IsRunning()) return;

    AndroidAppState* s = (AndroidAppState*)app->userData;
    int sensorFrameIndex;
    int eyeCount = 2;
    PxrPosef pose[eyeCount];
    PxrSensorState  sensorState = {};
    double predictedDisplayTimeMs = 0.0f;

    Pxr_BeginFrame();

    Pxr_GetPredictedDisplayTime(&predictedDisplayTimeMs);
    Pxr_GetPredictedMainSensorStateWithEyePose(predictedDisplayTimeMs, &sensorState, &sensorFrameIndex, eyeCount, pose);

    // Render two 10cm cube scaled by grabAction for each hand.
    for(int i = 0; i<PXR_CONTROLLER_COUNT; i++){
        if(s->handState[i]){
            PxrControllerTracking tracking;
            float sensorController[7];
            sensorController[0] = sensorState.pose.orientation.x;
            sensorController[1] = sensorState.pose.orientation.y;
            sensorController[2] = sensorState.pose.orientation.z;
            sensorController[3] = sensorState.pose.orientation.w;
            sensorController[4] = sensorState.pose.position.x;
            sensorController[5] = sensorState.pose.position.y;
            sensorController[6] = sensorState.pose.position.z;
            Pxr_GetControllerTrackingState(i, predictedDisplayTimeMs, sensorController, &tracking);
            cubes.push_back(Cube{ {{tracking.localControllerPose.pose.orientation.x,
                                           tracking.localControllerPose.pose.orientation.y,tracking.localControllerPose.pose.orientation.z,tracking.localControllerPose.pose.orientation.w},
                                          {tracking.localControllerPose.pose.position.x+((float)s->joystick[i].x),tracking.localControllerPose.pose.position.y+((float)s->joystick[i].y),tracking.localControllerPose.pose.position.z}},
                                          {s->handScale[i], s->handScale[i], s->handScale[i]}});
        }
    }

    PxrProjectionView layerView[eyeCount];
    for(int i = 0; i < eyeCount; i++) {
        float fovL,fovR,fovU,fovD;

        Pxr_GetFov(PxrEyeType(i),&fovL,&fovR,&fovU,&fovD);

        layerView[i].pose.orientation.x = pose[i].orientation.x;
        layerView[i].pose.orientation.y = pose[i].orientation.y;
        layerView[i].pose.orientation.z = pose[i].orientation.z;
        layerView[i].pose.orientation.w = pose[i].orientation.w;
        layerView[i].pose.position.x    = pose[i].position.x;
        layerView[i].pose.position.y    = pose[i].position.y;
        layerView[i].pose.position.z    = pose[i].position.z;
        layerView[i].fov.angleLeft      = fovL;
        layerView[i].fov.angleRight     = fovR;
        layerView[i].fov.angleDown      = fovD;
        layerView[i].fov.angleUp        = fovU;
        layerView[i].imageRect.x        = 0;
        layerView[i].imageRect.y        = 0;
        layerView[i].imageRect.width    = s->recommendW;
        layerView[i].imageRect.height   = s->recommendH;
    }

    int imageIndex = 0;
    Pxr_GetLayerNextImageIndex(0, &imageIndex);
    graphicsPlugin->RenderView_N(PXR_EYE_LEFT,  layerView, s->layerImages[PXR_EYE_LEFT][imageIndex],  cubes, SAMPLE_COUNT);
    graphicsPlugin->RenderView_N(PXR_EYE_RIGHT, layerView, s->layerImages[PXR_EYE_RIGHT][imageIndex], cubes, SAMPLE_COUNT);

    PxrLayerProjection layerProjection = {};
    layerProjection.header.layerId          = s->eyeLayerId;
    layerProjection.header.layerFlags       = 0;
    layerProjection.header.colorScale[0]    = 1.0f;
    layerProjection.header.colorScale[1]    = 1.0f;
    layerProjection.header.colorScale[2]    = 1.0f;
    layerProjection.header.colorScale[3]    = 1.0f;
    layerProjection.header.sensorFrameIndex = sensorFrameIndex;
    Pxr_SubmitLayer((PxrLayerHeader*)&layerProjection);
    Pxr_EndFrame();

    while(s->handCount)
    {
        cubes.pop_back();
        s->handCount--;
    }
}


/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(struct android_app* app) {
    try {
        JNIEnv* Env;
        AndroidAppState appState = {};

        app->activity->vm->AttachCurrentThread(&Env, nullptr);
        app->userData = &appState;
        app->onAppCmd = app_handle_cmd;

        init_scene(app);
        pxrapi_init(app);

        while (app->destroyRequested == 0) {
            // Read all pending events.
            for (;;) {
                int events;
                struct android_poll_source* source;
                // If the timeout is zero, returns immediately without blocking.
                // If the timeout is negative, waits indefinitely until an event appears.
                const int timeoutMilliseconds =
                    (!appState.resumed  && !Pxr_IsRunning() && app->destroyRequested == 0) ? -1 : 0;
                if (ALooper_pollAll(timeoutMilliseconds, nullptr, &events, (void**)&source) < 0) {
                    break;
                }

                // Process this event.
                if (source != nullptr) {
                    source->process(app, source);
                }
            }

            dispatch_events(app);
            render_frame(app);
        }

        pxrapi_deinit(app);

    } catch (const std::exception& ex) {
        Log::Write(Log::Level::Error, ex.what());
    } catch (...) {
        Log::Write(Log::Level::Error, "Unknown Error");
    }
    sleep(1);
    //exit needed to release so resouces
    exit(0);
}

