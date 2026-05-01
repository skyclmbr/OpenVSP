//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VRManager.h"

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

    struct EyeSwap
    {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        int32_t w = 0;
        int32_t h = 0;
        std::vector<XrSwapchainImageOpenGLKHR> swapImages;
        std::vector<GLuint> fbos;
    };
    EyeSwap eyes[2];
    bool swapchainsCreated = false;

    GLuint triVAO = 0;
    GLuint triVBO = 0;
    GLuint program = 0;
    GLint uMVP = -1;
    GLint uColor = -1;

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
        // Virtual Desktop appears to produce unstable behavior with STAGE in this prototype.
        // Force LOCAL for now to isolate world-lock stability issues.
        world.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        world.poseInReferenceSpace.orientation.w = 1.f;
        r = xrCreateReferenceSpace( session, &world, &stageSpace );
        if ( XR_FAILED( r ) )
        {
            PrintXrError( "xrCreateReferenceSpace(LOCAL)", r );
            return false;
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
        return true;
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
            glGenFramebuffers( imgCount, eyes[e].fbos.data() );
            for ( uint32_t i = 0; i < imgCount; ++i )
            {
                glBindFramebuffer( GL_FRAMEBUFFER, eyes[e].fbos[i] );
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eyes[e].swapImages[i].image, 0 );
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
uniform mat4 uMVP;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

        const char *fsSrc = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec3 uColor;
void main() {
  fragColor = vec4(uColor, 1.0);
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
        uColor = glGetUniformLocation( program, "uColor" );

        const float verts[] = {
            0.0f, 0.3f, -0.8f,
            -0.25f, -0.2f, -0.8f,
            0.25f, -0.2f, -0.8f
        };

        glGenVertexArrays( 1, &triVAO );
        glGenBuffers( 1, &triVBO );
        glBindVertexArray( triVAO );
        glBindBuffer( GL_ARRAY_BUFFER, triVBO );
        glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 3, nullptr );
        glBindVertexArray( 0 );

        return true;
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
                fprintf( stderr, "[VSP_VR] xrBeginSession succeeded.\n" );
            }

            if ( s->state == XR_SESSION_STATE_STOPPING )
            {
                if ( m_impl->sessionBegun )
                {
                    xrEndSession( m_impl->session );
                    m_impl->sessionBegun = false;
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
    static int s_frameCounter = 0;
    ++s_frameCounter;
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
        if ( s_frameCounter <= 240 )
        {
            fprintf( stderr, "[VSP_VR] frame %d: shouldRender=false\n", s_frameCounter );
        }
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
            if ( s_frameCounter <= 240 )
            {
                fprintf( stderr, "[VSP_VR] frame %d: invalid views count=%u flags=0x%llx (empty frame)\n",
                         s_frameCounter, viewCountOut, static_cast<unsigned long long>( viewState.viewStateFlags ) );
            }
        }

        XrFrameEndInfo endSkip{ XR_TYPE_FRAME_END_INFO };
        endSkip.displayTime = frameState.predictedDisplayTime;
        endSkip.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endSkip.layerCount = 0;
        endSkip.layers = nullptr;
        xrEndFrame( m_impl->session, &endSkip );
        return true;
    }

    if ( !m_impl->demoAnchorValid )
    {
        const XrPosef &headPose = views[0].pose;
        const glm::quat headQ( headPose.orientation.w, headPose.orientation.x,
                               headPose.orientation.y, headPose.orientation.z );
        const glm::vec3 headP( headPose.position.x, headPose.position.y, headPose.position.z );
        glm::vec3 fwd = glm::mat3_cast( headQ ) * glm::vec3( 0.f, 0.f, -1.f );
        if ( glm::length( fwd ) < 1.0e-4f )
        {
            fwd = glm::vec3( 0.f, 0.f, -1.f );
        }
        else
        {
            fwd = glm::normalize( fwd );
        }
        m_impl->demoAnchorPos = headP + fwd * 2.0f;
        m_impl->demoAnchorPos.y = headP.y - 0.15f;
        m_impl->demoAnchorValid = true;
    }
    else if ( s_frameCounter <= 60 )
    {
        fprintf( stderr, "[VSP_VR] frame %d: render views flags=0x%llx\n",
                 s_frameCounter, static_cast<unsigned long long>( viewState.viewStateFlags ) );
    }

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
        glDisable( GL_DEPTH_TEST );
        glClearColor( 0.03f, 0.03f, 0.06f, 1.f );
        glClear( GL_COLOR_BUFFER_BIT );

        const glm::mat4 viewMat = glm::inverse( XrPoseToMat4( views[eye].pose ) );
        const glm::mat4 proj = ProjectionFromFov( views[eye].fov, nearZ, farZ );
        const glm::mat4 model =
            glm::translate( glm::mat4( 1.f ), m_impl->demoAnchorPos ) *
            glm::scale( glm::mat4( 1.f ), glm::vec3( 0.55f ) );
        const glm::mat4 mvp = proj * viewMat * model;

        glUseProgram( m_impl->program );
        glUniformMatrix4fv( m_impl->uMVP, 1, GL_FALSE, glm::value_ptr( mvp ) );
        const float cr = ( eye == 0 ) ? 0.9f : 0.4f;
        const float cg = ( eye == 0 ) ? 0.4f : 0.85f;
        const float cb = 0.3f;
        glUniform3f( m_impl->uColor, cr, cg, cb );
        glBindVertexArray( m_impl->triVAO );
        const float triVerts[] = {
            0.0f, 0.45f, 0.0f,
            -0.4f, -0.35f, 0.0f,
            0.4f, -0.35f, 0.0f
        };
        glBindBuffer( GL_ARRAY_BUFFER, m_impl->triVBO );
        glBufferData( GL_ARRAY_BUFFER, sizeof( triVerts ), triVerts, GL_STREAM_DRAW );
        glDrawArrays( GL_TRIANGLES, 0, 3 );

        // Draw simple XYZ reference lines at the same anchor point.
        const float axisVerts[] = {
            0.0f, 0.0f, 0.0f,   0.8f, 0.0f, 0.0f, // +X
            0.0f, 0.0f, 0.0f,   0.0f, 0.8f, 0.0f, // +Y
            0.0f, 0.0f, 0.0f,   0.0f, 0.0f, -0.8f // -Z (forward)
        };
        glBufferData( GL_ARRAY_BUFFER, sizeof( axisVerts ), axisVerts, GL_STREAM_DRAW );
        glUniformMatrix4fv( m_impl->uMVP, 1, GL_FALSE, glm::value_ptr( mvp ) );
        glUniform3f( m_impl->uColor, 1.0f, 1.0f, 1.0f );
        glDrawArrays( GL_LINES, 0, 6 );

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
