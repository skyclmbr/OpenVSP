//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifndef VRIMGUIOVERLAY_H
#define VRIMGUIOVERLAY_H

#include <glm/mat4x4.hpp>

/// Phase 4: Dear ImGui rendered to an off-screen texture and drawn as a world-space quad in stage space.
class VRImGuiOverlay
{
public:
    VRImGuiOverlay();
    ~VRImGuiOverlay();

    VRImGuiOverlay( const VRImGuiOverlay & ) = delete;
    VRImGuiOverlay &operator=( const VRImGuiOverlay & ) = delete;

    bool Init( int fbWidth, int fbHeight );
    void Shutdown();

    void NewFrame( float deltaTimeSeconds );
    void RenderUiToTexture();

    /// Draw the UI texture as a vertical quad in stage space (meters), facing the stage origin.
    void DrawStageBillboard( const glm::mat4 &viewProj ) const;

    bool IsReady() const
    {
        return m_ready;
    }

private:
    bool m_ready = false;
    int m_fbW = 0;
    int m_fbH = 0;

    unsigned int m_fbo = 0;
    unsigned int m_colorTex = 0;
    unsigned int m_depthRb = 0;

    unsigned int m_billboardVAO = 0;
    unsigned int m_billboardVBO = 0;
    unsigned int m_billboardProgram = 0;
    int m_uBillboardMVP = -1;
};

#endif
