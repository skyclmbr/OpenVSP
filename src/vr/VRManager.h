//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifndef VRMANAGER_H
#define VRMANAGER_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdint>

// Phase 1: OpenXR session + stereo swapchains; demo triangle per eye (Windows OpenGL).

class VRManager
{
public:
    static VRManager &Get();

#ifdef _WIN32
    /// Bind to an existing WGL context (required for xrCreateSession on Windows).
    bool Init( HWND hwnd, HDC hdc, HGLRC glrc );
#else
    bool Init();
#endif

    void Shutdown();

    /// Pump OpenXR events; call once per frame.
    bool PollEvents();

    bool IsRunning() const
    {
        return m_running;
    }

    bool IsSessionRunning() const;

    /// Phase 1: render one stereo frame (colored triangle) into swapchains.
    bool RenderStereoDemo();

    /// Last predicted display time (nanoseconds), valid after a successful RenderStereoDemo begin.
    int64_t GetLastPredictedDisplayTime() const
    {
        return m_lastPredictedDisplayTime;
    }

private:
    VRManager() = default;
    ~VRManager();

    VRManager( const VRManager & ) = delete;
    VRManager &operator=( const VRManager & ) = delete;

    struct Impl;
    Impl *m_impl = nullptr;

    bool m_running = false;
    int64_t m_lastPredictedDisplayTime = 0;
};

#endif
