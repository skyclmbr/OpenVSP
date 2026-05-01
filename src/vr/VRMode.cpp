//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VRMode.h"

#include "VRManager.h"

#include <cstdio>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wingdi.h>

#include <GL/glew.h>
#include <GL/wglew.h>

namespace
{

static const wchar_t kWndClass[] = L"OpenVSP_VR_HiddenGL";

LRESULT CALLBACK VRHiddenWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    if ( msg == WM_DESTROY )
    {
        PostQuitMessage( 0 );
    }
    return DefWindowProcW( hwnd, msg, wParam, lParam );
}

/// Minimal WGL context (OpenGL 3.3 core) for OpenXR GL session binding.
static bool CreateHiddenGl33Context( HWND &outHwnd, HDC &outHdc, HGLRC &outRc )
{
    HINSTANCE hInst = GetModuleHandleW( nullptr );

    WNDCLASSW wc{};
    wc.lpfnWndProc = VRHiddenWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWndClass;
    if ( !RegisterClassW( &wc ) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
    {
        fprintf( stderr, "[VSP_VR] RegisterClassW failed.\n" );
        return false;
    }

    HWND hwnd = CreateWindowExW( 0, kWndClass, L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, hInst, nullptr );
    if ( !hwnd )
    {
        fprintf( stderr, "[VSP_VR] CreateWindowExW failed.\n" );
        return false;
    }

    HDC hdc = GetDC( hwnd );
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof( pfd );
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | 0x00000001; // PFD_DOUBLE_BUFFER
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;

    const int pf = ChoosePixelFormat( hdc, &pfd );
    if ( !pf || !SetPixelFormat( hdc, pf, &pfd ) )
    {
        fprintf( stderr, "[VSP_VR] SetPixelFormat failed.\n" );
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    HGLRC tmpRc = wglCreateContext( hdc );
    if ( !tmpRc || !wglMakeCurrent( hdc, tmpRc ) )
    {
        fprintf( stderr, "[VSP_VR] wglCreateContext / wglMakeCurrent failed.\n" );
        if ( tmpRc )
        {
            wglDeleteContext( tmpRc );
        }
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if ( glewErr != GLEW_OK )
    {
        fprintf( stderr, "[VSP_VR] glewInit failed: %s\n", glewGetErrorString( glewErr ) );
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( tmpRc );
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        ( PFNWGLCREATECONTEXTATTRIBSARBPROC )wglGetProcAddress( "wglCreateContextAttribsARB" );
    if ( !wglCreateContextAttribsARB )
    {
        fprintf( stderr, "[VSP_VR] wglCreateContextAttribsARB not available (need OpenGL 3.3).\n" );
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( tmpRc );
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    HGLRC rc = wglCreateContextAttribsARB( hdc, nullptr, attribs );
    wglMakeCurrent( nullptr, nullptr );
    wglDeleteContext( tmpRc );
    if ( !rc || !wglMakeCurrent( hdc, rc ) )
    {
        fprintf( stderr, "[VSP_VR] Failed to create OpenGL 3.3 core context.\n" );
        if ( rc )
        {
            wglDeleteContext( rc );
        }
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    glewErr = glewInit();
    if ( glewErr != GLEW_OK )
    {
        fprintf( stderr, "[VSP_VR] glewInit (after 3.3 context) failed: %s\n", glewGetErrorString( glewErr ) );
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( rc );
        ReleaseDC( hwnd, hdc );
        DestroyWindow( hwnd );
        return false;
    }

    outHwnd = hwnd;
    outHdc = hdc;
    outRc = rc;
    return true;
}

static void DestroyGlContext( HWND hwnd, HDC hdc, HGLRC rc )
{
    if ( rc )
    {
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( rc );
    }
    if ( hwnd && hdc )
    {
        ReleaseDC( hwnd, hdc );
    }
    if ( hwnd )
    {
        DestroyWindow( hwnd );
    }
}

} // namespace

#endif // _WIN32

int RunVRMode( int argc, char **argv )
{
    ( void )argc;
    ( void )argv;

#ifdef _WIN32

    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC glrc = nullptr;

    if ( !CreateHiddenGl33Context( hwnd, hdc, glrc ) )
    {
        return 1;
    }

    if ( !VRManager::Get().Init( hwnd, hdc, glrc ) )
    {
        DestroyGlContext( hwnd, hdc, glrc );
        return 1;
    }

    while ( VRManager::Get().IsRunning() )
    {
        MSG msg;
        while ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                VRManager::Get().Shutdown();
                DestroyGlContext( hwnd, hdc, glrc );
                return 0;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }

        if ( !VRManager::Get().PollEvents() )
        {
            break;
        }

        if ( VRManager::Get().IsSessionRunning() )
        {
            if ( !VRManager::Get().RenderStereoDemo() )
            {
                break;
            }
        }
        else
        {
            Sleep( 10 );
        }
    }

    VRManager::Get().Shutdown();
    DestroyGlContext( hwnd, hdc, glrc );
    return 0;

#else

    return VRManager::Get().Init() ? 0 : 1;

#endif
}

