//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VRManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>

#include <GL/glew.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "VSP_Geom_API.h"
#include "Vec3d.h"

namespace
{

static void PrintXrError( const char *what, XrResult r )
{
    char buf[XR_MAX_RESULT_STRING_SIZE];
    if ( XR_SUCCEEDED( xrResultToString( XR_NULL_HANDLE, r, buf ) ) )
    {
        fprintf( stderr, "[VSP_VR] %s failed: %s\n", what, buf );
    }
    else
    {
        fprintf( stderr, "[VSP_VR] %s failed: error code %d\n", what, ( int )r );
    }
}

static bool HasExtension( const std::vector<XrExtensionProperties> &exts, const char *name )
{
    for ( const auto &e : exts )
    {
        if ( strcmp( e.extensionName, name ) == 0 )
        {
            return true;
        }
    }
    return false;
}

static const char *SessionStateName( XrSessionState s )
{
    switch ( s )
    {
    case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "OTHER";
    }
}

static glm::mat4 XrPoseToMat4( const XrPosef &pose )
{
    const glm::quat q( pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z );
    glm::mat4 R = glm::mat4_cast( q );
    glm::mat4 T = glm::translate( glm::mat4( 1.f ), glm::vec3( pose.position.x, pose.position.y, pose.position.z ) );
    return T * R;
}

static glm::mat4 ProjectionFromFov( const XrFovf &fov, float nearZ, float farZ )
{
    const float l = nearZ * tanf( fov.angleLeft );
    const float r = nearZ * tanf( fov.angleRight );
    const float d = nearZ * tanf( fov.angleDown );
    const float u = nearZ * tanf( fov.angleUp );
    return glm::frustum( l, r, d, u, nearZ, farZ );
}

static GLuint CompileShader( GLenum type, const char *src )
{
    GLuint s = glCreateShader( type );
    glShaderSource( s, 1, &src, nullptr );
    glCompileShader( s );
    GLint ok = 0;
    glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
    if ( !ok )
    {
        char log[1024];
        glGetShaderInfoLog( s, sizeof( log ), nullptr, log );
        fprintf( stderr, "[VSP_VR] Shader compile failed: %s\n", log );
        glDeleteShader( s );
        return 0;
    }
    return s;
}

static GLuint LinkProgram( GLuint vs, GLuint fs )
{
    GLuint p = glCreateProgram();
    glAttachShader( p, vs );
    glAttachShader( p, fs );
    glLinkProgram( p );
    GLint ok = 0;
    glGetProgramiv( p, GL_LINK_STATUS, &ok );
    if ( !ok )
    {
        char log[1024];
        glGetProgramInfoLog( p, sizeof( log ), nullptr, log );
        fprintf( stderr, "[VSP_VR] Program link failed: %s\n", log );
        glDeleteProgram( p );
        return 0;
    }
    return p;
}

} // namespace

struct VRManager::Impl
{
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSpace stageSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;

    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool sessionBegun = false;
    bool demoAnchorValid = false;
    glm::vec3 demoAnchorPos = glm::vec3( 0.f, 1.35f, -2.5f );
    float demoScale = 0.55f;
    bool boundsUsedFallback = false;
    XrExtent2Df playAreaBounds{ 2.0f, 2.0f };
    bool cachedModelValid = false;
    std::vector<float> cachedModelVerts;
    std::vector<float> cachedModelNorms;
    std::vector<float> cachedWireVerts;
    std::vector<float> cachedWireColors;
    float cachedModelAlpha = 1.0f;
    bool cachedModelBoundsValid = false;
    glm::vec3 cachedModelMin = glm::vec3( 0.0f );
    glm::vec3 cachedModelMax = glm::vec3( 0.0f );
    glm::vec3 modelLocalOffset = glm::vec3( 0.0f );
    glm::vec3 modelTranslation = glm::vec3( 0.0f, 0.94f, -1.2f );
    glm::quat modelRotation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    float modelScale = 1.0f;
    bool zLock = false;

    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction gripPoseAction = XR_NULL_HANDLE;
    XrAction squeezeAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction thumbClickAction = XR_NULL_HANDLE;
    XrAction thumbstickAction = XR_NULL_HANDLE;
    XrPath leftHandPath = XR_NULL_PATH;
    XrPath rightHandPath = XR_NULL_PATH;
    XrSpace leftGripSpace = XR_NULL_HANDLE;
    XrSpace rightGripSpace = XR_NULL_HANDLE;

    struct HandPoseState
    {
        bool valid = false;
        glm::vec3 pos = glm::vec3( 0.0f );
        glm::quat rot = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        glm::vec3 filteredPos = glm::vec3( 0.0f );
        glm::quat filteredRot = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        bool filterInitialized = false;
        float squeeze = 0.0f;
    } hands[2];

    bool singleGrabActive = false;
    int singleGrabHand = -1;
    glm::vec3 singleGrabStartPos = glm::vec3( 0.0f );
    glm::quat singleGrabStartRot = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    glm::vec3 singleGrabBaseTranslation = glm::vec3( 0.0f );
    glm::quat singleGrabBaseRotation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );

    bool twoGrabActive = false;
    float twoGrabInitialSpan = 1.0f;
    glm::vec3 twoGrabInitialMidpoint = glm::vec3( 0.0f );
    glm::vec3 twoGrabBaseTranslation = glm::vec3( 0.0f );
    float twoGrabBaseScale = 1.0f;
    int inputLogCounter = 0;
    XrTime lastInteractionTime = 0;
    float leftThumbHeldTime = 0.0f;
    bool leftThumbDownPrev = false;
    bool zLockToggledThisPress = false;
    bool rightThumbDownPrev = false;
    float rightThumbHeldTime = 0.0f;
    int viewPresetIndex = 0;

    struct EyeSwap
    {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        int32_t w = 0;
        int32_t h = 0;
        std::vector<XrSwapchainImageOpenGLKHR> swapImages;
        std::vector<GLuint> fbos;
        std::vector<GLuint> depthRbos;
    };
    EyeSwap eyes[2];
    bool swapchainsCreated = false;

    GLuint triVAO = 0;
    GLuint triVBO = 0;
    GLuint program = 0;
    GLint uMVP = -1;
    GLint uModel = -1;
    GLint uColor = -1;
    GLint uAlpha = -1;
    GLint uLightDir = -1;

    bool CreateInstance()
    {
        uint32_t extCount = 0;
        xrEnumerateInstanceExtensionProperties( nullptr, 0, &extCount, nullptr );
        std::vector<XrExtensionProperties> exts( extCount, { XR_TYPE_EXTENSION_PROPERTIES } );
        xrEnumerateInstanceExtensionProperties( nullptr, extCount, &extCount, exts.data() );

        if ( !HasExtension( exts, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) )
        {
            fprintf( stderr, "[VSP_VR] Active OpenXR runtime does not expose %s. Install a PC VR runtime (e.g. Meta Quest Link / SteamVR) and set it as the active OpenXR runtime.\n",
                     XR_KHR_OPENGL_ENABLE_EXTENSION_NAME );
            return false;
        }

        std::vector<const char *> want;
        want.push_back( XR_KHR_OPENGL_ENABLE_EXTENSION_NAME );

        XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
        strncpy( ci.applicationInfo.applicationName, "OpenVSP", XR_MAX_APPLICATION_NAME_SIZE );
        ci.applicationInfo.applicationName[XR_MAX_APPLICATION_NAME_SIZE - 1] = '\0';
        ci.applicationInfo.applicationVersion = 1;
        strncpy( ci.applicationInfo.engineName, "OpenVSP-VR", XR_MAX_ENGINE_NAME_SIZE );
        ci.applicationInfo.engineName[XR_MAX_ENGINE_NAME_SIZE - 1] = '\0';
        ci.applicationInfo.engineVersion = 1;
        ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        ci.enabledExtensionCount = ( uint32_t )want.size();
        ci.enabledExtensionNames = want.data();

        XrResult r = xrCreateInstance( &ci, &instance );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateInstance", r );
            fprintf( stderr, "[VSP_VR] If no runtime is active, install Oculus / SteamVR OpenXR and select it as the system OpenXR runtime.\n" );
            return false;
        }
        return true;
    }

    bool CreateSystem()
    {
        XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrResult r = xrGetSystem( instance, &sgi, &systemId );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrGetSystem", r );
            return false;
        }
        return true;
    }

    bool CreateSession( HWND hwnd, HDC hdc, HGLRC glrc )
    {
        PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetReq = nullptr;
        xrGetInstanceProcAddr( instance, "xrGetOpenGLGraphicsRequirementsKHR",
                               reinterpret_cast<PFN_xrVoidFunction *>( &pfnGetReq ) );
        if ( pfnGetReq )
        {
            XrGraphicsRequirementsOpenGLKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
            pfnGetReq( instance, systemId, &req );
        }

        XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
        binding.hDC = hdc;
        binding.hGLRC = glrc;

        XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
        sci.next = &binding;
        sci.systemId = systemId;

        XrResult r = xrCreateSession( instance, &sci, &session );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateSession", r );
            return false;
        }

        XrReferenceSpaceCreateInfo world{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        world.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        world.poseInReferenceSpace.orientation.w = 1.f;
        r = xrCreateReferenceSpace( session, &world, &stageSpace );
        if ( XR_FAILED( r ) )
        {
            fprintf( stderr, "[VSP_VR] STAGE space unavailable, falling back to LOCAL.\n" );
            world.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            r = xrCreateReferenceSpace( session, &world, &stageSpace );
            if ( XR_FAILED( r ) )
            {
                PrintXrError( "xrCreateReferenceSpace(LOCAL)", r );
                return false;
            }
        }

        XrReferenceSpaceCreateInfo view{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        view.poseInReferenceSpace.orientation.w = 1.f;
        r = xrCreateReferenceSpace( session, &view, &viewSpace );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateReferenceSpace(VIEW)", r );
            return false;
        }

        ( void )hwnd;
        return true;
    }

    bool BeginSession()
    {
        XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult r = xrBeginSession( session, &bi );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrBeginSession", r );
            return false;
        }
        sessionBegun = true;
        demoAnchorValid = false;
        cachedModelValid = false;
        cachedModelVerts.clear();
        return true;
    }

    bool QueryPlayAreaBounds()
    {
        XrExtent2Df bounds{};
        XrResult r = xrGetReferenceSpaceBoundsRect( session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds );
        if ( XR_SUCCEEDED( r ) && bounds.width > 0.01f && bounds.height > 0.01f )
        {
            playAreaBounds = bounds;
            boundsUsedFallback = false;
            fprintf( stderr, "[VSP_VR] Room bounds: %.2fm x %.2fm (STAGE)\n", bounds.width, bounds.height );
            return true;
        }

        // Common when no room-scale boundary is configured.
        playAreaBounds.width = 2.0f;
        playAreaBounds.height = 2.0f;
        boundsUsedFallback = true;
        fprintf( stderr, "[VSP_VR] Room bounds unavailable; using fallback 2.0m x 2.0m.\n" );
        return false;
    }

    void PlaceDemoInRoom()
    {
        // Fit inside ~60% of the shorter play-area side.
        const float fitSpan = std::min( playAreaBounds.width, playAreaBounds.height ) * 0.6f;
        float modelSpan = 0.8f;
        modelLocalOffset = glm::vec3( 0.0f );

        if ( cachedModelBoundsValid )
        {
            const glm::vec3 ext = cachedModelMax - cachedModelMin;
            modelSpan = std::max( 0.2f, std::max( ext.x, ext.z ) ); // floor footprint in XZ

            // Center laterally and shift bottom to local Y=0 so full model sits above floor.
            modelLocalOffset.x = -0.5f * ( cachedModelMin.x + cachedModelMax.x );
            modelLocalOffset.z = -0.5f * ( cachedModelMin.z + cachedModelMax.z );
            modelLocalOffset.y = -cachedModelMin.y;
        }

        demoScale = std::max( 0.05f, fitSpan / modelSpan );

        // Keep object in front of the user; lift ~3 ft for easier debugging views.
        demoAnchorPos = glm::vec3( 0.0f, 0.94f, -1.2f );
        demoAnchorValid = true;
        modelTranslation = demoAnchorPos;
        modelRotation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        modelScale = demoScale;
        fprintf( stderr, "[VSP_VR] Placed demo: scale=%.3f at (%.2f, %.2f, %.2f)%s\n",
                 demoScale, demoAnchorPos.x, demoAnchorPos.y, demoAnchorPos.z,
                 boundsUsedFallback ? " [fallback bounds]" : "" );
    }

    bool CreateInputActions()
    {
        xrStringToPath( instance, "/user/hand/left", &leftHandPath );
        xrStringToPath( instance, "/user/hand/right", &rightHandPath );

        XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
        strncpy( asci.actionSetName, "vspvr_input", XR_MAX_ACTION_SET_NAME_SIZE );
        strncpy( asci.localizedActionSetName, "OpenVSP VR Input", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE );
        asci.priority = 0;
        XrResult r = xrCreateActionSet( instance, &asci, &actionSet );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateActionSet", r );
            return false;
        }

        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strncpy( aci.actionName, "grip_pose", XR_MAX_ACTION_NAME_SIZE );
        strncpy( aci.localizedActionName, "Grip Pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
        XrPath handSubactions[2] = { leftHandPath, rightHandPath };
        aci.countSubactionPaths = 2;
        aci.subactionPaths = handSubactions;
        r = xrCreateAction( actionSet, &aci, &gripPoseAction );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateAction(grip_pose)", r );
            return false;
        }

        XrActionCreateInfo sci{ XR_TYPE_ACTION_CREATE_INFO };
        sci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        strncpy( sci.actionName, "squeeze", XR_MAX_ACTION_NAME_SIZE );
        strncpy( sci.localizedActionName, "Squeeze", XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
        sci.countSubactionPaths = 2;
        sci.subactionPaths = handSubactions;
        r = xrCreateAction( actionSet, &sci, &squeezeAction );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateAction(squeeze)", r );
            return false;
        }

        XrActionCreateInfo tci{ XR_TYPE_ACTION_CREATE_INFO };
        tci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        strncpy( tci.actionName, "trigger", XR_MAX_ACTION_NAME_SIZE );
        strncpy( tci.localizedActionName, "Trigger", XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
        tci.countSubactionPaths = 2;
        tci.subactionPaths = handSubactions;
        r = xrCreateAction( actionSet, &tci, &triggerAction );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateAction(trigger)", r );
            return false;
        }

        XrActionCreateInfo bci{ XR_TYPE_ACTION_CREATE_INFO };
        bci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strncpy( bci.actionName, "thumb_click", XR_MAX_ACTION_NAME_SIZE );
        strncpy( bci.localizedActionName, "Thumbstick Click", XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
        bci.countSubactionPaths = 2;
        bci.subactionPaths = handSubactions;
        r = xrCreateAction( actionSet, &bci, &thumbClickAction );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateAction(thumb_click)", r );
            return false;
        }

        XrActionCreateInfo vci{ XR_TYPE_ACTION_CREATE_INFO };
        vci.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        strncpy( vci.actionName, "thumbstick", XR_MAX_ACTION_NAME_SIZE );
        strncpy( vci.localizedActionName, "Thumbstick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
        vci.countSubactionPaths = 2;
        vci.subactionPaths = handSubactions;
        r = xrCreateAction( actionSet, &vci, &thumbstickAction );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateAction(thumbstick)", r );
            return false;
        }

        XrPath simpleProfile = XR_NULL_PATH;
        XrPath touchProfile = XR_NULL_PATH;
        XrPath leftGrip = XR_NULL_PATH, rightGrip = XR_NULL_PATH;
        XrPath leftAim = XR_NULL_PATH, rightAim = XR_NULL_PATH;
        XrPath leftSelect = XR_NULL_PATH, rightSelect = XR_NULL_PATH;
        XrPath leftSqueezeValue = XR_NULL_PATH, rightSqueezeValue = XR_NULL_PATH;
        XrPath leftTriggerValue = XR_NULL_PATH, rightTriggerValue = XR_NULL_PATH;
        XrPath leftThumbClick = XR_NULL_PATH, rightThumbClick = XR_NULL_PATH;
        XrPath leftThumbstick = XR_NULL_PATH, rightThumbstick = XR_NULL_PATH;
        xrStringToPath( instance, "/interaction_profiles/khr/simple_controller", &simpleProfile );
        xrStringToPath( instance, "/interaction_profiles/oculus/touch_controller", &touchProfile );
        xrStringToPath( instance, "/user/hand/left/input/grip/pose", &leftGrip );
        xrStringToPath( instance, "/user/hand/right/input/grip/pose", &rightGrip );
        xrStringToPath( instance, "/user/hand/left/input/aim/pose", &leftAim );
        xrStringToPath( instance, "/user/hand/right/input/aim/pose", &rightAim );
        xrStringToPath( instance, "/user/hand/left/input/select/value", &leftSelect );   // Simple profile fallback.
        xrStringToPath( instance, "/user/hand/right/input/select/value", &rightSelect );
        xrStringToPath( instance, "/user/hand/left/input/squeeze/value", &leftSqueezeValue ); // Touch grip analog.
        xrStringToPath( instance, "/user/hand/right/input/squeeze/value", &rightSqueezeValue );
        xrStringToPath( instance, "/user/hand/left/input/trigger/value", &leftTriggerValue );
        xrStringToPath( instance, "/user/hand/right/input/trigger/value", &rightTriggerValue );
        xrStringToPath( instance, "/user/hand/left/input/thumbstick/click", &leftThumbClick );
        xrStringToPath( instance, "/user/hand/right/input/thumbstick/click", &rightThumbClick );
        xrStringToPath( instance, "/user/hand/left/input/thumbstick", &leftThumbstick );
        xrStringToPath( instance, "/user/hand/right/input/thumbstick", &rightThumbstick );

        XrActionSuggestedBinding simpleBinds[5] = {
            { gripPoseAction, leftGrip },
            { gripPoseAction, rightGrip },
            { gripPoseAction, leftAim },
            { squeezeAction, leftSelect },
            { squeezeAction, rightSelect }
        };
        XrInteractionProfileSuggestedBinding simpleSb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        simpleSb.interactionProfile = simpleProfile;
        simpleSb.countSuggestedBindings = 5;
        simpleSb.suggestedBindings = simpleBinds;
        xrSuggestInteractionProfileBindings( instance, &simpleSb );

        XrActionSuggestedBinding touchBinds[12] = {
            { gripPoseAction, leftGrip },
            { gripPoseAction, rightGrip },
            { gripPoseAction, leftAim },
            { gripPoseAction, rightAim },
            { squeezeAction, leftSqueezeValue },
            { squeezeAction, rightSqueezeValue },
            { triggerAction, leftTriggerValue },
            { triggerAction, rightTriggerValue },
            { thumbClickAction, leftThumbClick },
            { thumbClickAction, rightThumbClick },
            { thumbstickAction, leftThumbstick },
            { thumbstickAction, rightThumbstick }
        };
        XrInteractionProfileSuggestedBinding touchSb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        touchSb.interactionProfile = touchProfile;
        touchSb.countSuggestedBindings = 12;
        touchSb.suggestedBindings = touchBinds;
        xrSuggestInteractionProfileBindings( instance, &touchSb );

        XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        attach.countActionSets = 1;
        attach.actionSets = &actionSet;
        r = xrAttachSessionActionSets( session, &attach );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrAttachSessionActionSets", r );
            return false;
        }

        XrActionSpaceCreateInfo ls{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        ls.action = gripPoseAction;
        ls.subactionPath = leftHandPath;
        ls.poseInActionSpace.orientation.w = 1.0f;
        r = xrCreateActionSpace( session, &ls, &leftGripSpace );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateActionSpace(left)", r );
            return false;
        }

        XrActionSpaceCreateInfo rs{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        rs.action = gripPoseAction;
        rs.subactionPath = rightHandPath;
        rs.poseInActionSpace.orientation.w = 1.0f;
        r = xrCreateActionSpace( session, &rs, &rightGripSpace );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateActionSpace(right)", r );
            return false;
        }
        return true;
    }

    void ResetInteractionState()
    {
        singleGrabActive = false;
        twoGrabActive = false;
        singleGrabHand = -1;
        lastInteractionTime = 0;
        leftThumbHeldTime = 0.0f;
        leftThumbDownPrev = false;
        zLockToggledThisPress = false;
        rightThumbDownPrev = false;
        rightThumbHeldTime = 0.0f;
    }

    bool TryGetHeadPoseInStage( XrTime displayTime, glm::vec3 &headPosOut ) const
    {
        if ( session == XR_NULL_HANDLE || viewSpace == XR_NULL_HANDLE || stageSpace == XR_NULL_HANDLE )
        {
            return false;
        }

        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        const XrResult r = xrLocateSpace( viewSpace, stageSpace, displayTime, &loc );
        if ( XR_FAILED( r ) )
        {
            return false;
        }

        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        if ( ( loc.locationFlags & need ) != need )
        {
            return false;
        }

        headPosOut = glm::vec3( loc.pose.position.x, loc.pose.position.y, loc.pose.position.z );
        return true;
    }

    void RecalibrateFloorFromHead( XrTime displayTime )
    {
        glm::vec3 headPos( 0.0f );
        if ( !TryGetHeadPoseInStage( displayTime, headPos ) )
        {
            fprintf( stderr, "[VSP_VR] Floor recal skipped (head pose unavailable).\n" );
            return;
        }

        // In LOCAL fallback, infer floor from current HMD height.
        constexpr float kAssumedEyeHeightM = 1.65f;
        const float estimatedFloorY = headPos.y - kAssumedEyeHeightM;
        modelTranslation.y -= estimatedFloorY;
        demoAnchorPos.y -= estimatedFloorY;
        fprintf( stderr, "[VSP_VR] Floor recalibrated (deltaY=%.3f).\n", -estimatedFloorY );
    }

    void UpdateHandState( int handIdx, XrSpace handSpace, XrTime displayTime )
    {
        hands[handIdx].valid = false;

        XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = squeezeAction;
        gi.subactionPath = ( handIdx == 0 ) ? leftHandPath : rightHandPath;
        XrActionStateFloat sf{ XR_TYPE_ACTION_STATE_FLOAT };
        xrGetActionStateFloat( session, &gi, &sf );
        hands[handIdx].squeeze = sf.currentState;

        XrActionStateGetInfo ti{ XR_TYPE_ACTION_STATE_GET_INFO };
        ti.action = triggerAction;
        ti.subactionPath = ( handIdx == 0 ) ? leftHandPath : rightHandPath;
        XrActionStateFloat tf{ XR_TYPE_ACTION_STATE_FLOAT };
        xrGetActionStateFloat( session, &ti, &tf );
        hands[handIdx].squeeze = std::max( hands[handIdx].squeeze, tf.currentState );

        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        XrResult r = xrLocateSpace( handSpace, stageSpace, displayTime, &loc );
        if ( XR_FAILED( r ) )
        {
            return;
        }
        const XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ( ( loc.locationFlags & need ) != need )
        {
            return;
        }
        hands[handIdx].valid = true;
        hands[handIdx].pos = glm::vec3( loc.pose.position.x, loc.pose.position.y, loc.pose.position.z );
        hands[handIdx].rot = glm::normalize( glm::quat( loc.pose.orientation.w, loc.pose.orientation.x,
                                                        loc.pose.orientation.y, loc.pose.orientation.z ) );

        // Lightweight filtering to reduce controller micro-jitter while preserving responsiveness.
        constexpr float kPosAlpha = 0.25f;
        constexpr float kRotAlpha = 0.22f;
        if ( !hands[handIdx].filterInitialized )
        {
            hands[handIdx].filteredPos = hands[handIdx].pos;
            hands[handIdx].filteredRot = hands[handIdx].rot;
            hands[handIdx].filterInitialized = true;
        }
        else
        {
            hands[handIdx].filteredPos = glm::mix( hands[handIdx].filteredPos, hands[handIdx].pos, kPosAlpha );
            hands[handIdx].filteredRot = glm::normalize( glm::slerp( hands[handIdx].filteredRot, hands[handIdx].rot, kRotAlpha ) );
        }
    }

    void UpdateInteraction( XrTime displayTime )
    {
        if ( actionSet == XR_NULL_HANDLE )
        {
            return;
        }

        float dt = 1.0f / 72.0f;
        if ( lastInteractionTime != 0 && displayTime > lastInteractionTime )
        {
            dt = static_cast<float>( ( displayTime - lastInteractionTime ) * 1.0e-9 );
            dt = std::clamp( dt, 0.0f, 0.1f );
        }
        lastInteractionTime = displayTime;

        XrActiveActionSet active{ actionSet, XR_NULL_PATH };
        XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO };
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        xrSyncActions( session, &sync );

        UpdateHandState( 0, leftGripSpace, displayTime );
        UpdateHandState( 1, rightGripSpace, displayTime );

        XrActionStateGetInfo leftThumbInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        leftThumbInfo.action = thumbClickAction;
        leftThumbInfo.subactionPath = leftHandPath;
        XrActionStateBoolean leftThumbState{ XR_TYPE_ACTION_STATE_BOOLEAN };
        xrGetActionStateBoolean( session, &leftThumbInfo, &leftThumbState );
        const bool leftThumbDown = leftThumbState.isActive && leftThumbState.currentState;

        XrActionStateGetInfo rightThumbInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        rightThumbInfo.action = thumbClickAction;
        rightThumbInfo.subactionPath = rightHandPath;
        XrActionStateBoolean rightThumbState{ XR_TYPE_ACTION_STATE_BOOLEAN };
        xrGetActionStateBoolean( session, &rightThumbInfo, &rightThumbState );
        const bool rightThumbDown = rightThumbState.isActive && rightThumbState.currentState;

        XrActionStateGetInfo rightStickInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        rightStickInfo.action = thumbstickAction;
        rightStickInfo.subactionPath = rightHandPath;
        XrActionStateVector2f rightStick{ XR_TYPE_ACTION_STATE_VECTOR2F };
        xrGetActionStateVector2f( session, &rightStickInfo, &rightStick );

        if ( leftThumbDown )
        {
            leftThumbHeldTime += dt;
            if ( leftThumbHeldTime >= 1.0f && !zLockToggledThisPress )
            {
                zLock = !zLock;
                zLockToggledThisPress = true;
                fprintf( stderr, "[VSP_VR] Z-lock %s\n", zLock ? "ON" : "OFF" );
            }
        }
        else if ( leftThumbDownPrev )
        {
            if ( !zLockToggledThisPress && leftThumbHeldTime < 1.0f )
            {
                // Short press: reset view to room-fitted baseline.
                PlaceDemoInRoom();
                fprintf( stderr, "[VSP_VR] Reset view.\n" );
            }
            leftThumbHeldTime = 0.0f;
            zLockToggledThisPress = false;
        }
        leftThumbDownPrev = leftThumbDown;

        if ( rightStick.isActive )
        {
            const float dead = 0.15f;
            const float stickX = ( std::abs( rightStick.currentState.x ) > dead ) ? rightStick.currentState.x : 0.0f;
            const float stickY = ( std::abs( rightStick.currentState.y ) > dead ) ? rightStick.currentState.y : 0.0f;

            // Right thumbstick Y: zoom (scale in place).
            if ( stickY != 0.0f )
            {
                const float zoomGain = 1.5f;
                const float factor = std::exp( stickY * zoomGain * dt );
                modelScale = std::clamp( modelScale * factor, 0.01f, 50.0f );
            }

            // Right thumbstick X: yaw around world up (+Y).
            if ( stickX != 0.0f )
            {
                const float yawRate = 1.7f; // rad/s at full deflection
                const float yaw = stickX * yawRate * dt;
                const glm::quat yawRot = glm::angleAxis( yaw, glm::vec3( 0.0f, 1.0f, 0.0f ) );
                modelRotation = glm::normalize( yawRot * modelRotation );
            }
        }

        if ( rightThumbDown )
        {
            rightThumbHeldTime += dt;
        }
        else if ( rightThumbDownPrev )
        {
            if ( rightThumbHeldTime < 1.0f )
            {
                // Short right click cycles preset views.
                static const float kPresetYaw[4] = { 0.0f, 1.5707963f, 3.1415926f, 0.7853982f };
                viewPresetIndex = ( viewPresetIndex + 1 ) % 4;
                modelRotation = glm::angleAxis( kPresetYaw[viewPresetIndex], glm::vec3( 0.0f, 1.0f, 0.0f ) );
                fprintf( stderr, "[VSP_VR] View preset %d\n", viewPresetIndex );
            }
            else
            {
                // Long right click recalibrates floor estimate.
                RecalibrateFloorFromHead( displayTime );
            }
            rightThumbHeldTime = 0.0f;
        }
        rightThumbDownPrev = rightThumbDown;

        if ( ( ++inputLogCounter % 180 ) == 0 )
        {
            fprintf( stderr, "[VSP_VR] input L(valid=%d sq=%.2f) R(valid=%d sq=%.2f)\n",
                     hands[0].valid ? 1 : 0, hands[0].squeeze,
                     hands[1].valid ? 1 : 0, hands[1].squeeze );
        }

        const bool leftGrab = hands[0].valid && hands[0].squeeze > 0.85f;
        const bool rightGrab = hands[1].valid && hands[1].squeeze > 0.85f;

        if ( leftGrab && rightGrab )
        {
            if ( !twoGrabActive )
            {
                twoGrabActive = true;
                singleGrabActive = false;
                const glm::vec3 d = hands[1].filteredPos - hands[0].filteredPos;
                twoGrabInitialSpan = std::max( 0.01f, glm::length( d ) );
                twoGrabInitialMidpoint = 0.5f * ( hands[0].filteredPos + hands[1].filteredPos );
                twoGrabBaseTranslation = modelTranslation;
                twoGrabBaseScale = modelScale;
            }
            const glm::vec3 d = hands[1].filteredPos - hands[0].filteredPos;
            const float currentSpan = std::max( 0.01f, glm::length( d ) );
            const float scaleFactor = currentSpan / std::max( 0.01f, twoGrabInitialSpan );
            modelScale = std::clamp( twoGrabBaseScale * scaleFactor, 0.01f, 50.0f );

            const glm::vec3 midpoint = 0.5f * ( hands[0].filteredPos + hands[1].filteredPos );
            glm::vec3 midDelta = midpoint - twoGrabInitialMidpoint;
            if ( glm::length( midDelta ) < 0.004f )
            {
                midDelta = glm::vec3( 0.0f );
            }
            if ( zLock )
            {
                // Z-lock constrains rotation only; keep 3D translation enabled.
                modelTranslation = twoGrabBaseTranslation + midDelta * scaleFactor;
            }
            else
            {
                modelTranslation = twoGrabBaseTranslation + midDelta * scaleFactor;
            }
            return;
        }
        else
        {
            twoGrabActive = false;
        }

        int activeHand = -1;
        if ( leftGrab )
        {
            activeHand = 0;
        }
        else if ( rightGrab )
        {
            activeHand = 1;
        }

        if ( activeHand >= 0 )
        {
            if ( !singleGrabActive || singleGrabHand != activeHand )
            {
                singleGrabActive = true;
                singleGrabHand = activeHand;
                singleGrabStartPos = hands[activeHand].filteredPos;
                singleGrabStartRot = hands[activeHand].filteredRot;
                singleGrabBaseTranslation = modelTranslation;
                singleGrabBaseRotation = modelRotation;
            }

            glm::vec3 deltaPos = hands[activeHand].filteredPos - singleGrabStartPos;
            if ( glm::length( deltaPos ) < 0.003f )
            {
                deltaPos = glm::vec3( 0.0f );
            }
            const glm::quat deltaRot = glm::normalize( hands[activeHand].filteredRot * glm::inverse( singleGrabStartRot ) );

            if ( zLock )
            {
                // Z-lock constrains rotation only; keep 3D translation enabled.
                modelTranslation = singleGrabBaseTranslation + deltaPos;

                const float yaw = atan2f( 2.0f * ( deltaRot.w * deltaRot.y + deltaRot.x * deltaRot.z ),
                                          1.0f - 2.0f * ( deltaRot.y * deltaRot.y + deltaRot.z * deltaRot.z ) );
                const float yawFiltered = ( std::abs( yaw ) < 0.012f ) ? 0.0f : yaw;
                const glm::quat yawOnly = glm::angleAxis( yawFiltered, glm::vec3( 0.0f, 1.0f, 0.0f ) );
                modelRotation = glm::normalize( yawOnly * singleGrabBaseRotation );
            }
            else
            {
                const float w = std::clamp( deltaRot.w, -1.0f, 1.0f );
                const float ang = 2.0f * acosf( w );
                glm::quat appliedRot = deltaRot;
                if ( ang < 0.012f )
                {
                    appliedRot = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
                }
                modelTranslation = singleGrabBaseTranslation + deltaPos;
                modelRotation = glm::normalize( appliedRot * singleGrabBaseRotation );
            }
        }
        else
        {
            singleGrabActive = false;
            singleGrabHand = -1;
        }
    }

    void DestroySwapchains()
    {
        for ( int e = 0; e < 2; ++e )
        {
            for ( GLuint fbo : eyes[e].fbos )
            {
                if ( fbo )
                {
                    glDeleteFramebuffers( 1, &fbo );
                }
            }
            eyes[e].fbos.clear();
            for ( GLuint rbo : eyes[e].depthRbos )
            {
                if ( rbo )
                {
                    glDeleteRenderbuffers( 1, &rbo );
                }
            }
            eyes[e].depthRbos.clear();
            eyes[e].swapImages.clear();
            if ( eyes[e].swapchain != XR_NULL_HANDLE )
            {
                xrDestroySwapchain( eyes[e].swapchain );
                eyes[e].swapchain = XR_NULL_HANDLE;
            }
            eyes[e].w = eyes[e].h = 0;
        }
        swapchainsCreated = false;
    }

    bool CreateSwapchainsIfReady()
    {
        if ( swapchainsCreated )
        {
            return true;
        }

        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr );
        if ( viewCount < 2 )
        {
            fprintf( stderr, "[VSP_VR] Expected 2 views for stereo, got %u.\n", viewCount );
            return false;
        }

        std::vector<XrViewConfigurationView> views( viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW } );
        xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data() );

        for ( int e = 0; e < 2; ++e )
        {
            eyes[e].w = views[e].recommendedImageRectWidth;
            eyes[e].h = views[e].recommendedImageRectHeight;

            uint32_t fmtCount = 0;
            xrEnumerateSwapchainFormats( session, 0, &fmtCount, nullptr );
            std::vector<int64_t> formats( fmtCount );
            xrEnumerateSwapchainFormats( session, fmtCount, &fmtCount, formats.data() );

            int64_t chosen = 0;
            for ( int64_t f : formats )
            {
                if ( f == GL_SRGB8_ALPHA8 || f == GL_RGBA8 )
                {
                    chosen = f;
                    break;
                }
            }
            if ( chosen == 0 && !formats.empty() )
            {
                chosen = formats[0];
            }
            if ( chosen == 0 )
            {
                fprintf( stderr, "[VSP_VR] No compatible OpenGL swapchain format.\n" );
                return false;
            }

            XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            sci.arraySize = 1;
            sci.mipCount = 1;
            sci.faceCount = 1;
            sci.format = chosen;
            sci.width = ( uint32_t )eyes[e].w;
            sci.height = ( uint32_t )eyes[e].h;
            sci.sampleCount = views[e].recommendedSwapchainSampleCount ? views[e].recommendedSwapchainSampleCount : 1;
            sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

            XrResult r = xrCreateSwapchain( session, &sci, &eyes[e].swapchain );
            if ( XR_FAILED( r ) )
            {
                PrintXrError( "xrCreateSwapchain", r );
                DestroySwapchains();
                return false;
            }

            uint32_t imgCount = 0;
            xrEnumerateSwapchainImages( eyes[e].swapchain, 0, &imgCount, nullptr );
            eyes[e].swapImages.resize( imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR } );
            xrEnumerateSwapchainImages( eyes[e].swapchain, imgCount, &imgCount,
                                        reinterpret_cast<XrSwapchainImageBaseHeader *>( eyes[e].swapImages.data() ) );

            eyes[e].fbos.resize( imgCount );
            eyes[e].depthRbos.resize( imgCount );
            glGenFramebuffers( imgCount, eyes[e].fbos.data() );
            glGenRenderbuffers( imgCount, eyes[e].depthRbos.data() );
            for ( uint32_t i = 0; i < imgCount; ++i )
            {
                glBindFramebuffer( GL_FRAMEBUFFER, eyes[e].fbos[i] );
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eyes[e].swapImages[i].image, 0 );
                glBindRenderbuffer( GL_RENDERBUFFER, eyes[e].depthRbos[i] );
                glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, eyes[e].w, eyes[e].h );
                glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, eyes[e].depthRbos[i] );
                GLenum st = glCheckFramebufferStatus( GL_FRAMEBUFFER );
                if ( st != GL_FRAMEBUFFER_COMPLETE )
                {
                    fprintf( stderr, "[VSP_VR] Eye %d FBO incomplete (0x%x).\n", e, ( unsigned )st );
                    DestroySwapchains();
                    return false;
                }
            }
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        }

        swapchainsCreated = true;
        return true;
    }

    bool EnsureTriangleResources()
    {
        if ( program )
        {
            return true;
        }

        const char *vsSrc = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec3 aColor;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNorm;
out vec3 vColor;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vNorm = mat3(uModel) * aNorm;
  vColor = aColor;
}
)GLSL";

        const char *fsSrc = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec3 uColor;
uniform float uAlpha;
uniform vec3 uLightDir;
in vec3 vNorm;
in vec3 vColor;
void main() {
  vec3 n = normalize(vNorm);
  float ndl = max(dot(n, normalize(uLightDir)), 0.0);
  float lit = 0.25 + 0.75 * ndl;
  fragColor = vec4(vColor * lit, uAlpha);
}
)GLSL";

        GLuint vs = CompileShader( GL_VERTEX_SHADER, vsSrc );
        GLuint fs = CompileShader( GL_FRAGMENT_SHADER, fsSrc );
        if ( !vs || !fs )
        {
            return false;
        }
        program = LinkProgram( vs, fs );
        glDeleteShader( vs );
        glDeleteShader( fs );
        if ( !program )
        {
            return false;
        }
        uMVP = glGetUniformLocation( program, "uMVP" );
        uModel = glGetUniformLocation( program, "uModel" );
        uColor = glGetUniformLocation( program, "uColor" );
        uAlpha = glGetUniformLocation( program, "uAlpha" );
        uLightDir = glGetUniformLocation( program, "uLightDir" );

        glGenVertexArrays( 1, &triVAO );
        glGenBuffers( 1, &triVBO );
        glBindVertexArray( triVAO );
        glBindBuffer( GL_ARRAY_BUFFER, triVBO );
        glBufferData( GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 9, reinterpret_cast<void *>( 0 ) );
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 9, reinterpret_cast<void *>( sizeof( float ) * 3 ) );
        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 9, reinterpret_cast<void *>( sizeof( float ) * 6 ) );
        glBindVertexArray( 0 );

        return true;
    }

    void DrawVertexStream( const glm::mat4 &mvp, const glm::mat4 &modelMat,
                           const std::vector<float> &verts, const std::vector<float> &norms,
                           const std::vector<float> &colors,
                           GLenum prim, const glm::vec3 &color, float alpha )
    {
        if ( verts.empty() )
        {
            return;
        }
        std::vector<float> interleaved;
        interleaved.reserve( ( verts.size() / 3 ) * 9 );
        const bool hasNorms = ( norms.size() == verts.size() );
        const bool hasColors = ( colors.size() == verts.size() );
        for ( size_t i = 0; i < verts.size() / 3; ++i )
        {
            interleaved.push_back( verts[i * 3 + 0] );
            interleaved.push_back( verts[i * 3 + 1] );
            interleaved.push_back( verts[i * 3 + 2] );
            if ( hasNorms )
            {
                interleaved.push_back( norms[i * 3 + 0] );
                interleaved.push_back( norms[i * 3 + 1] );
                interleaved.push_back( norms[i * 3 + 2] );
            }
            else
            {
                interleaved.insert( interleaved.end(), { 0.0f, 1.0f, 0.0f } );
            }
            if ( hasColors )
            {
                interleaved.push_back( colors[i * 3 + 0] );
                interleaved.push_back( colors[i * 3 + 1] );
                interleaved.push_back( colors[i * 3 + 2] );
            }
            else
            {
                interleaved.push_back( color.r );
                interleaved.push_back( color.g );
                interleaved.push_back( color.b );
            }
        }
        glUseProgram( program );
        glUniformMatrix4fv( uMVP, 1, GL_FALSE, glm::value_ptr( mvp ) );
        glUniformMatrix4fv( uModel, 1, GL_FALSE, glm::value_ptr( modelMat ) );
        glUniform3f( uColor, color.r, color.g, color.b );
        glUniform1f( uAlpha, alpha );
        glUniform3f( uLightDir, 0.35f, 0.8f, 0.45f );
        glBindVertexArray( triVAO );
        glBindBuffer( GL_ARRAY_BUFFER, triVBO );
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeof( float ) * interleaved.size() ), interleaved.data(), GL_STREAM_DRAW );
        glDrawArrays( prim, 0, static_cast<GLsizei>( verts.size() / 3 ) );
    }

    static void AppendPoint( std::vector<float> &dst, const vec3d &p )
    {
        dst.push_back( static_cast<float>( p.x() ) );
        dst.push_back( static_cast<float>( p.y() ) );
        dst.push_back( static_cast<float>( p.z() ) );
    }

    static glm::vec3 ConvertVspToVrModel( const vec3d &p )
    {
        // User preference: treat OpenVSP +Z as up in VR.
        // This is a -90deg rotation around +X: (x, y, z) -> (x, z, -y).
        return glm::vec3( static_cast<float>( p.x() ),
                          static_cast<float>( p.z() ),
                          static_cast<float>( -p.y() ) );
    }

    static glm::vec3 ConvertVspNormalToVrModel( const vec3d &n )
    {
        const glm::vec3 v( static_cast<float>( n.x() ),
                           static_cast<float>( n.z() ),
                           static_cast<float>( -n.y() ) );
        const float len2 = glm::dot( v, v );
        if ( len2 < 1.0e-12f )
        {
            return glm::vec3( 0.0f, 1.0f, 0.0f );
        }
        return glm::normalize( v );
    }

    bool BuildSampledModelMesh()
    {
        cachedModelVerts.clear();
        cachedModelNorms.clear();
        cachedWireVerts.clear();
        cachedWireColors.clear();
        cachedModelBoundsValid = false;
        cachedModelAlpha = 1.0f;
        std::vector<std::string> geoms = vsp::FindGeoms();
        for ( const std::string &gid : geoms )
        {
            int drawType = vsp::GEOM_DRAW_SHADE;
            vec3d wireColor( 0.0, 0.0, 1.0 ); // Match VSP default wire color fallback.
            double alpha = 1.0;
            bool hasDrawType = false;
            bool hasAlpha = false;
            bool hasWireColor = false;
            const std::vector<std::string> parmIDs = vsp::GetGeomParmIDs( gid );
            for ( const std::string &pid : parmIDs )
            {
                const std::string pname = vsp::GetParmName( pid );
                const std::string gname = vsp::GetParmGroupName( pid );
                const std::string dgname = vsp::GetParmDisplayGroupName( pid );
                std::string group = gname + " " + dgname;
                std::transform( group.begin(), group.end(), group.begin(), []( unsigned char c )
                                { return static_cast<char>( std::tolower( c ) ); } );

                if ( pname == "DrawType" )
                {
                    drawType = static_cast<int>( std::lround( vsp::GetParmVal( pid ) ) );
                    hasDrawType = true;
                }
                if ( pname == "Alpha" && group.find( "material" ) != std::string::npos )
                {
                    alpha = std::clamp( vsp::GetParmVal( pid ), 0.0, 1.0 );
                    hasAlpha = true;
                }
                if ( group.find( "wire" ) != std::string::npos )
                {
                    if ( pname == "R" || pname == "Red" )
                    {
                        wireColor.set_x( vsp::GetParmVal( pid ) / 255.0 );
                        hasWireColor = true;
                    }
                    else if ( pname == "G" || pname == "Green" )
                    {
                        wireColor.set_y( vsp::GetParmVal( pid ) / 255.0 );
                        hasWireColor = true;
                    }
                    else if ( pname == "B" || pname == "Blue" )
                    {
                        wireColor.set_z( vsp::GetParmVal( pid ) / 255.0 );
                        hasWireColor = true;
                    }
                }
            }
            ( void )hasWireColor;
            if ( !hasDrawType )
            {
                drawType = vsp::GEOM_DRAW_SHADE;
            }
            if ( !hasAlpha )
            {
                alpha = 1.0;
            }
            // Avoid forcing whole-model translucency from one bad/aux value in quick-path aggregation.
            cachedModelAlpha = std::max( cachedModelAlpha, static_cast<float>( alpha ) );

            // Use total surfaces so planar/axial symmetry copies are included.
            const int numSurf = std::max( 0, vsp::GetTotalNumSurfs( gid ) );
            for ( int s = 0; s < numSurf; ++s )
            {
                std::vector<double> utess;
                std::vector<double> wtess;
                vsp::GetUWTess01( gid, s, utess, wtess );
                if ( utess.size() < 2 || wtess.size() < 2 )
                {
                    continue;
                }

                for ( size_t i = 0; i + 1 < utess.size(); ++i )
                {
                    for ( size_t j = 0; j + 1 < wtess.size(); ++j )
                    {
                        const vec3d p00 = vsp::CompPnt01( gid, s, utess[i], wtess[j] );
                        const vec3d p10 = vsp::CompPnt01( gid, s, utess[i + 1], wtess[j] );
                        const vec3d p01 = vsp::CompPnt01( gid, s, utess[i], wtess[j + 1] );
                        const vec3d p11 = vsp::CompPnt01( gid, s, utess[i + 1], wtess[j + 1] );
                        const vec3d n00_api = vsp::CompNorm01( gid, s, utess[i], wtess[j] );
                        const vec3d n10_api = vsp::CompNorm01( gid, s, utess[i + 1], wtess[j] );
                        const vec3d n01_api = vsp::CompNorm01( gid, s, utess[i], wtess[j + 1] );
                        const vec3d n11_api = vsp::CompNorm01( gid, s, utess[i + 1], wtess[j + 1] );
                        const glm::vec3 v00 = ConvertVspToVrModel( p00 );
                        const glm::vec3 v10 = ConvertVspToVrModel( p10 );
                        const glm::vec3 v01 = ConvertVspToVrModel( p01 );
                        const glm::vec3 v11 = ConvertVspToVrModel( p11 );
                        const glm::vec3 n00 = ConvertVspNormalToVrModel( n00_api );
                        const glm::vec3 n10 = ConvertVspNormalToVrModel( n10_api );
                        const glm::vec3 n01 = ConvertVspNormalToVrModel( n01_api );
                        const glm::vec3 n11 = ConvertVspNormalToVrModel( n11_api );

                        auto append_glm = [this]( const glm::vec3 &p )
                        {
                            cachedModelVerts.push_back( p.x );
                            cachedModelVerts.push_back( p.y );
                            cachedModelVerts.push_back( p.z );
                            if ( !cachedModelBoundsValid )
                            {
                                cachedModelMin = cachedModelMax = p;
                                cachedModelBoundsValid = true;
                            }
                            else
                            {
                                cachedModelMin = glm::min( cachedModelMin, p );
                                cachedModelMax = glm::max( cachedModelMax, p );
                            }
                        };
                        const bool drawShade = ( drawType == vsp::GEOM_DRAW_SHADE || drawType == vsp::GEOM_DRAW_TEXTURE || drawType == vsp::GEOM_DRAW_HIDDEN );
                        // Hidden-line parity needs a dedicated pass; for now, avoid full grid wire overlay in hidden mode.
                        const bool drawWire = ( drawType == vsp::GEOM_DRAW_WIRE );
                        if ( drawShade )
                        {
                            append_glm( v00 ); append_glm( v10 ); append_glm( v11 );
                            cachedModelNorms.insert( cachedModelNorms.end(), { n00.x, n00.y, n00.z, n10.x, n10.y, n10.z, n11.x, n11.y, n11.z } );

                            append_glm( v00 ); append_glm( v11 ); append_glm( v01 );
                            cachedModelNorms.insert( cachedModelNorms.end(), { n00.x, n00.y, n00.z, n11.x, n11.y, n11.z, n01.x, n01.y, n01.z } );
                        }
                        if ( drawWire )
                        {
                            auto addWire = [this, &wireColor]( const glm::vec3 &a, const glm::vec3 &b )
                            {
                                cachedWireVerts.insert( cachedWireVerts.end(), { a.x, a.y, a.z, b.x, b.y, b.z } );
                                for ( int i = 0; i < 2; ++i )
                                {
                                    cachedWireColors.push_back( static_cast<float>( wireColor.x() ) );
                                    cachedWireColors.push_back( static_cast<float>( wireColor.y() ) );
                                    cachedWireColors.push_back( static_cast<float>( wireColor.z() ) );
                                }
                            };
                            addWire( v00, v10 );
                            addWire( v10, v11 );
                            addWire( v11, v01 );
                            addWire( v01, v00 );
                        }
                    }
                }
            }
        }
        cachedModelValid = ( !cachedModelVerts.empty() || !cachedWireVerts.empty() );
        if ( !cachedModelValid )
        {
            fprintf( stderr, "[VSP_VR] No geom mesh sampled from OpenVSP scene.\n" );
        }
        return cachedModelValid;
    }

    bool DrawVehicleModel( const glm::mat4 &mvp, const glm::mat4 &modelMat )
    {
        if ( !cachedModelValid )
        {
            BuildSampledModelMesh();
        }
        if ( !cachedModelValid )
        {
            return false;
        }
        bool drew = false;
        // Slight blue tint to make geometry easier to distinguish from background.
        if ( !cachedModelVerts.empty() )
        {
            if ( cachedModelAlpha < 0.999f )
            {
                glEnable( GL_BLEND );
                glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            }
            DrawVertexStream( mvp, modelMat, cachedModelVerts, cachedModelNorms, std::vector<float>{},
                              GL_TRIANGLES, glm::vec3( 0.65f, 0.75f, 0.95f ), cachedModelAlpha );
            if ( cachedModelAlpha < 0.999f )
            {
                glDisable( GL_BLEND );
            }
            drew = true;
        }
        if ( !cachedWireVerts.empty() )
        {
            glLineWidth( 1.5f );
            DrawVertexStream( mvp, modelMat, cachedWireVerts, std::vector<float>{}, cachedWireColors,
                              GL_LINES, glm::vec3( 1.0f, 1.0f, 1.0f ), 1.0f );
            drew = true;
        }
        return drew;
    }

    void DestroyGLResources()
    {
        if ( triVBO )
        {
            glDeleteBuffers( 1, &triVBO );
            triVBO = 0;
        }
        if ( triVAO )
        {
            glDeleteVertexArrays( 1, &triVAO );
            triVAO = 0;
        }
        if ( program )
        {
            glDeleteProgram( program );
            program = 0;
        }
    }

    void DestroyXrSession()
    {
        DestroySwapchains();
        if ( leftGripSpace != XR_NULL_HANDLE )
        {
            xrDestroySpace( leftGripSpace );
            leftGripSpace = XR_NULL_HANDLE;
        }
        if ( rightGripSpace != XR_NULL_HANDLE )
        {
            xrDestroySpace( rightGripSpace );
            rightGripSpace = XR_NULL_HANDLE;
        }
        if ( viewSpace != XR_NULL_HANDLE )
        {
            xrDestroySpace( viewSpace );
            viewSpace = XR_NULL_HANDLE;
        }
        if ( stageSpace != XR_NULL_HANDLE )
        {
            xrDestroySpace( stageSpace );
            stageSpace = XR_NULL_HANDLE;
        }
        if ( session != XR_NULL_HANDLE )
        {
            if ( sessionBegun )
            {
                xrEndSession( session );
                sessionBegun = false;
            }
            xrDestroySession( session );
            session = XR_NULL_HANDLE;
        }
        if ( squeezeAction != XR_NULL_HANDLE )
        {
            xrDestroyAction( squeezeAction );
            squeezeAction = XR_NULL_HANDLE;
        }
        if ( thumbClickAction != XR_NULL_HANDLE )
        {
            xrDestroyAction( thumbClickAction );
            thumbClickAction = XR_NULL_HANDLE;
        }
        if ( thumbstickAction != XR_NULL_HANDLE )
        {
            xrDestroyAction( thumbstickAction );
            thumbstickAction = XR_NULL_HANDLE;
        }
        if ( triggerAction != XR_NULL_HANDLE )
        {
            xrDestroyAction( triggerAction );
            triggerAction = XR_NULL_HANDLE;
        }
        if ( gripPoseAction != XR_NULL_HANDLE )
        {
            xrDestroyAction( gripPoseAction );
            gripPoseAction = XR_NULL_HANDLE;
        }
        if ( actionSet != XR_NULL_HANDLE )
        {
            xrDestroyActionSet( actionSet );
            actionSet = XR_NULL_HANDLE;
        }
        systemId = XR_NULL_SYSTEM_ID;
    }

    void DestroyInstance()
    {
        if ( instance != XR_NULL_HANDLE )
        {
            xrDestroyInstance( instance );
            instance = XR_NULL_HANDLE;
        }
    }
};

VRManager::~VRManager()
{
    Shutdown();
}

VRManager &VRManager::Get()
{
    static VRManager s_inst;
    return s_inst;
}

bool VRManager::Init( HWND hwnd, HDC hdc, HGLRC glrc )
{
    Shutdown();

    fprintf( stderr, "[VSP_VR] Init: creating OpenXR instance...\n" );
    m_impl = new Impl();
    if ( !m_impl->CreateInstance() )
    {
        delete m_impl;
        m_impl = nullptr;
        m_running = false;
        return false;
    }
    fprintf( stderr, "[VSP_VR] Init: selecting OpenXR system...\n" );
    if ( !m_impl->CreateSystem() )
    {
        m_impl->DestroyInstance();
        delete m_impl;
        m_impl = nullptr;
        m_running = false;
        return false;
    }
    fprintf( stderr, "[VSP_VR] Init: creating OpenXR session...\n" );
    if ( !m_impl->CreateSession( hwnd, hdc, glrc ) )
    {
        m_impl->DestroyInstance();
        delete m_impl;
        m_impl = nullptr;
        m_running = false;
        return false;
    }
    if ( !m_impl->CreateInputActions() )
    {
        m_impl->DestroyXrSession();
        m_impl->DestroyInstance();
        delete m_impl;
        m_impl = nullptr;
        m_running = false;
        return false;
    }

    m_running = true;
    fprintf( stderr, "[VSP_VR] Init complete. Waiting for session state READY...\n" );
    return true;
}

void VRManager::Shutdown()
{
    if ( !m_impl )
    {
        return;
    }
    m_impl->DestroyGLResources();
    m_impl->DestroySwapchains();
    m_impl->DestroyXrSession();
    m_impl->DestroyInstance();
    delete m_impl;
    m_impl = nullptr;
    m_running = false;
}

bool VRManager::PollEvents()
{
    if ( !m_impl )
    {
        return false;
    }

    XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
    for ( ;; )
    {
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult r = xrPollEvent( m_impl->instance, &ev );
        if ( r == XR_EVENT_UNAVAILABLE )
        {
            break;
        }
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrPollEvent", r );
            m_running = false;
            return false;
        }

        switch ( ev.type )
        {
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            fprintf( stderr, "[VSP_VR] OpenXR instance loss pending.\n" );
            m_running = false;
            break;
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        {
            const auto *s = reinterpret_cast<const XrEventDataSessionStateChanged *>( &ev );
            m_impl->sessionState = s->state;
            fprintf( stderr, "[VSP_VR] Session state -> %s\n", SessionStateName( s->state ) );

            if ( s->state == XR_SESSION_STATE_READY && !m_impl->sessionBegun )
            {
                // Recreate frame resources on each new begin cycle to avoid stale state
                // after headset idle/remount transitions.
                m_impl->DestroySwapchains();
                if ( !m_impl->BeginSession() )
                {
                    m_running = false;
                    return false;
                }
                m_impl->QueryPlayAreaBounds();
                m_impl->BuildSampledModelMesh();
                m_impl->PlaceDemoInRoom();
                fprintf( stderr, "[VSP_VR] xrBeginSession succeeded.\n" );
            }

            if ( s->state == XR_SESSION_STATE_STOPPING )
            {
                if ( m_impl->sessionBegun )
                {
                    xrEndSession( m_impl->session );
                    m_impl->sessionBegun = false;
                    m_impl->ResetInteractionState();
                    fprintf( stderr, "[VSP_VR] xrEndSession done (STOPPING).\n" );
                }
            }
            else if ( s->state == XR_SESSION_STATE_EXITING || s->state == XR_SESSION_STATE_LOSS_PENDING )
            {
                m_running = false;
            }

            break;
        }
        default:
            break;
        }
    }
    return m_running;
}

bool VRManager::IsSessionRunning() const
{
    if ( !m_impl )
    {
        return false;
    }
    return m_impl->sessionBegun &&
           m_impl->sessionState != XR_SESSION_STATE_STOPPING &&
           m_impl->sessionState != XR_SESSION_STATE_EXITING &&
           m_impl->sessionState != XR_SESSION_STATE_LOSS_PENDING;
}

bool VRManager::RenderStereoDemo()
{
    if ( !m_impl || !m_running )
    {
        return false;
    }

    if ( !IsSessionRunning() )
    {
        return true;
    }

    if ( !m_impl->swapchainsCreated )
    {
        if ( !m_impl->CreateSwapchainsIfReady() )
        {
            return false;
        }
        if ( !m_impl->EnsureTriangleResources() )
        {
            return false;
        }
    }

    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState{ XR_TYPE_FRAME_STATE };
    XrResult r = xrWaitFrame( m_impl->session, &waitInfo, &frameState );
    if ( XR_FAILED( r ) )
    {
        PrintXrError( "xrWaitFrame", r );
        return false;
    }

    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    r = xrBeginFrame( m_impl->session, &beginInfo );
    if ( XR_FAILED( r ) )
    {
        PrintXrError( "xrBeginFrame", r );
        return false;
    }

    if ( !frameState.shouldRender )
    {
        XrFrameEndInfo endSkip{ XR_TYPE_FRAME_END_INFO };
        endSkip.displayTime = frameState.predictedDisplayTime;
        endSkip.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endSkip.layerCount = 0;
        endSkip.layers = nullptr;
        xrEndFrame( m_impl->session, &endSkip );
        return true;
    }

    XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = frameState.predictedDisplayTime;
    vli.space = m_impl->stageSpace;

    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    uint32_t viewCountOut = 0;
    uint32_t viewCap = 2;
    XrView views[2]{ { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    views[0].pose.orientation.w = 1.0f;
    views[1].pose.orientation.w = 1.0f;

    r = xrLocateViews( m_impl->session, &vli, &viewState, viewCap, &viewCountOut, views );
    const XrViewStateFlags needValidFlags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT;

    if ( XR_FAILED( r ) || viewCountOut != 2 ||
         ( viewState.viewStateFlags & needValidFlags ) != needValidFlags )
    {
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrLocateViews", r );
        }
        else
        {
            fprintf( stderr, "[VSP_VR] Invalid view flags=0x%llx, submitting empty frame.\n",
                     static_cast<unsigned long long>( viewState.viewStateFlags ) );
        }

        XrFrameEndInfo endSkip{ XR_TYPE_FRAME_END_INFO };
        endSkip.displayTime = frameState.predictedDisplayTime;
        endSkip.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endSkip.layerCount = 0;
        endSkip.layers = nullptr;
        xrEndFrame( m_impl->session, &endSkip );
        return true;
    }

    m_impl->UpdateInteraction( frameState.predictedDisplayTime );

    XrCompositionLayerProjectionView projViews[2]{ { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };

    const float nearZ = 0.05f;
    const float farZ = 100.f;

    for ( uint32_t eye = 0; eye < 2; ++eye )
    {
        uint32_t imgIndex = 0;
        XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        r = xrAcquireSwapchainImage( m_impl->eyes[eye].swapchain, &ai, &imgIndex );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrAcquireSwapchainImage", r );
            XrFrameEndInfo endFail{ XR_TYPE_FRAME_END_INFO };
            xrEndFrame( m_impl->session, &endFail );
            return false;
        }

        XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = XR_INFINITE_DURATION;
        r = xrWaitSwapchainImage( m_impl->eyes[eye].swapchain, &wi );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrWaitSwapchainImage", r );
            XrFrameEndInfo endFail{ XR_TYPE_FRAME_END_INFO };
            xrEndFrame( m_impl->session, &endFail );
            return false;
        }

        glBindFramebuffer( GL_FRAMEBUFFER, m_impl->eyes[eye].fbos[imgIndex] );
        glViewport( 0, 0, m_impl->eyes[eye].w, m_impl->eyes[eye].h );
        glEnable( GL_DEPTH_TEST );
        glDepthMask( GL_TRUE );
        glClearDepth( 1.0 );
        glClearColor( 0.03f, 0.03f, 0.06f, 1.f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        const glm::mat4 viewMat = glm::inverse( XrPoseToMat4( views[eye].pose ) );
        const glm::mat4 proj = ProjectionFromFov( views[eye].fov, nearZ, farZ );
        const glm::mat4 model =
            glm::translate( glm::mat4( 1.f ), m_impl->modelTranslation ) *
            glm::mat4_cast( m_impl->modelRotation ) *
            glm::scale( glm::mat4( 1.f ), glm::vec3( m_impl->modelScale ) ) *
            glm::translate( glm::mat4( 1.f ), m_impl->modelLocalOffset );
        const glm::mat4 mvp = proj * viewMat * model;

        const bool drewModel = m_impl->DrawVehicleModel( mvp, model );

        if ( !drewModel )
        {
            // Fallback marker until full scene bridge is available for every draw type.
            const std::vector<float> triVerts = {
                0.0f, 0.45f, 0.0f,
                -0.4f, -0.35f, 0.0f,
                0.4f, -0.35f, 0.0f
            };
            const float cr = ( eye == 0 ) ? 0.9f : 0.4f;
            const float cg = ( eye == 0 ) ? 0.4f : 0.85f;
            const float cb = 0.3f;
            const std::vector<float> triNorms = {
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f
            };
            m_impl->DrawVertexStream( mvp, model, triVerts, triNorms, std::vector<float>{},
                                      GL_TRIANGLES, glm::vec3( cr, cg, cb ), 1.0f );
        }

        glBindVertexArray( 0 );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );

        XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage( m_impl->eyes[eye].swapchain, &ri );

        projViews[eye].pose = views[eye].pose;
        projViews[eye].fov = views[eye].fov;
        projViews[eye].subImage.swapchain = m_impl->eyes[eye].swapchain;
        projViews[eye].subImage.imageRect.offset = { 0, 0 };
        projViews[eye].subImage.imageRect.extent = { m_impl->eyes[eye].w, m_impl->eyes[eye].h };
        projViews[eye].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layer.space = m_impl->stageSpace;
    layer.viewCount = 2;
    layer.views = projViews;

    const XrCompositionLayerBaseHeader *layers[1] = { reinterpret_cast<const XrCompositionLayerBaseHeader *>( &layer ) };

    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;

    r = xrEndFrame( m_impl->session, &endInfo );
    if ( XR_FAILED( r ) )
    {
        PrintXrError( "xrEndFrame", r );
        return false;
    }

    return true;
}

#else // !_WIN32

#include <cstdio>

struct VRManager::Impl
{
};

VRManager::~VRManager()
{
    Shutdown();
}

VRManager &VRManager::Get()
{
    static VRManager s_inst;
    return s_inst;
}

bool VRManager::Init()
{
    fprintf( stderr, "[VSP_VR] OpenXR VR path is only implemented on Windows in this build.\n" );
    m_running = false;
    return false;
}

void VRManager::Shutdown()
{
    m_running = false;
}

bool VRManager::PollEvents()
{
    return false;
}

bool VRManager::IsSessionRunning() const
{
    return false;
}

bool VRManager::RenderStereoDemo()
{
    return false;
}

#endif // _WIN32
