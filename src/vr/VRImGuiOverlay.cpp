//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VRImGuiOverlay.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "VSP_Geom_API.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

namespace
{

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
        fprintf( stderr, "[VSP_VR] billboard shader compile failed: %s\n", log );
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
        fprintf( stderr, "[VSP_VR] billboard program link failed: %s\n", log );
        glDeleteProgram( p );
        return 0;
    }
    return p;
}

static int GeomTreeDepth( const std::string &geomId )
{
    int depth = 0;
    int guard = 0;
    std::string p = vsp::GetGeomParent( geomId );
    while ( !p.empty() && guard++ < 64 )
    {
        ++depth;
        p = vsp::GetGeomParent( p );
    }
    return depth;
}

} // namespace

VRImGuiOverlay::VRImGuiOverlay() = default;

VRImGuiOverlay::~VRImGuiOverlay()
{
    Shutdown();
}

bool VRImGuiOverlay::Init( int fbWidth, int fbHeight )
{
    Shutdown();

    if ( fbWidth < 64 || fbHeight < 64 )
    {
        fprintf( stderr, "[VSP_VR] VRImGuiOverlay: framebuffer too small.\n" );
        return false;
    }

    m_fbW = fbWidth;
    m_fbH = fbHeight;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( static_cast<float>( m_fbW ), static_cast<float>( m_fbH ) );

    ImGui::StyleColorsDark();

    ImGui_ImplOpenGL3_Init( "#version 330" );

    glGenFramebuffers( 1, &m_fbo );
    glGenTextures( 1, &m_colorTex );
    glBindTexture( GL_TEXTURE_2D, m_colorTex );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, m_fbW, m_fbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );

    glGenRenderbuffers( 1, &m_depthRb );
    glBindRenderbuffer( GL_RENDERBUFFER, m_depthRb );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_fbW, m_fbH );

    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0 );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb );
    const GLenum fboStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
    {
        fprintf( stderr, "[VSP_VR] VRImGui overlay FBO incomplete (0x%x).\n", static_cast<unsigned>( fboStatus ) );
        Shutdown();
        return false;
    }

    const char *vsBillboard = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
  vUV = aUV;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

    const char *fsBillboard = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
  // ImGui FBO is top-down vs GL texture convention; flip V only. U matches quad winding.
  vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
  fragColor = texture(uTex, uv);
}
)GLSL";

    GLuint vs = CompileShader( GL_VERTEX_SHADER, vsBillboard );
    GLuint fs = CompileShader( GL_FRAGMENT_SHADER, fsBillboard );
    if ( !vs || !fs )
    {
        Shutdown();
        return false;
    }
    m_billboardProgram = LinkProgram( vs, fs );
    glDeleteShader( vs );
    glDeleteShader( fs );
    if ( !m_billboardProgram )
    {
        Shutdown();
        return false;
    }
    m_uBillboardMVP = glGetUniformLocation( m_billboardProgram, "uMVP" );
    const GLint uTex = glGetUniformLocation( m_billboardProgram, "uTex" );
    glUseProgram( m_billboardProgram );
    glUniform1i( uTex, 0 );
    glUseProgram( 0 );

    // Two triangles, XY plane (normal +Z); positioned via MVP in DrawStageBillboard.
    const float verts[] = {
        // x    y    z     u    v
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };

    glGenVertexArrays( 1, &m_billboardVAO );
    glGenBuffers( 1, &m_billboardVBO );
    glBindVertexArray( m_billboardVAO );
    glBindBuffer( GL_ARRAY_BUFFER, m_billboardVBO );
    glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW );
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 5, reinterpret_cast<void *>( 0 ) );
    glEnableVertexAttribArray( 1 );
    glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( float ) * 5, reinterpret_cast<void *>( sizeof( float ) * 3 ) );
    glBindVertexArray( 0 );

    m_ready = true;
    fprintf( stderr, "[VSP_VR] Phase 4: ImGui overlay initialized (%dx%d).\n", m_fbW, m_fbH );
    return true;
}

void VRImGuiOverlay::Shutdown()
{
    if ( ImGui::GetCurrentContext() != nullptr )
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }

    if ( m_billboardVBO )
    {
        glDeleteBuffers( 1, &m_billboardVBO );
        m_billboardVBO = 0;
    }
    if ( m_billboardVAO )
    {
        glDeleteVertexArrays( 1, &m_billboardVAO );
        m_billboardVAO = 0;
    }
    if ( m_billboardProgram )
    {
        glDeleteProgram( m_billboardProgram );
        m_billboardProgram = 0;
    }
    if ( m_depthRb )
    {
        glDeleteRenderbuffers( 1, &m_depthRb );
        m_depthRb = 0;
    }
    if ( m_colorTex )
    {
        glDeleteTextures( 1, &m_colorTex );
        m_colorTex = 0;
    }
    if ( m_fbo )
    {
        glDeleteFramebuffers( 1, &m_fbo );
        m_fbo = 0;
    }

    m_ready = false;
    m_fbW = 0;
    m_fbH = 0;
    m_selectedGeomId.clear();
}

void VRImGuiOverlay::NewFrame( float deltaTimeSeconds )
{
    if ( !m_ready )
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2( static_cast<float>( m_fbW ), static_cast<float>( m_fbH ) );
    io.DeltaTime = ( deltaTimeSeconds > 1e-6f ) ? deltaTimeSeconds : ( 1.0f / 72.0f );

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos( ImVec2( 16.f, 16.f ), ImGuiCond_Always );
    ImGui::SetNextWindowSize( ImVec2( static_cast<float>( m_fbW - 32 ), static_cast<float>( m_fbH - 32 ) ), ImGuiCond_Always );

    ImGui::Begin( "OpenVSP VR", nullptr,
                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse );

    ImGui::TextUnformatted( "Geometry Browser" );
    ImGui::Separator();

    const float footerH = 52.0f;
    const float listH = ImGui::GetContentRegionAvail().y - footerH;
    ImGui::BeginChild( "geom_browser_list", ImVec2( 0.0f, listH ), ImGuiChildFlags_Border,
                       ImGuiWindowFlags_None );

    const std::vector<std::string> geoms = vsp::FindGeoms();
    if ( geoms.empty() )
    {
        ImGui::TextDisabled( "(no geometry loaded)" );
    }
    else
    {
        for ( const std::string &gid : geoms )
        {
            const int depth = GeomTreeDepth( gid );
            const std::string typeName = vsp::GetGeomTypeName( gid );
            const std::string dispName = vsp::GetGeomName( gid );

            std::string label = typeName;
            label += "  |  ";
            label += dispName;
            label += "  |  ";
            label += gid;

            ImGui::PushID( gid.c_str() );
            if ( depth > 0 )
            {
                ImGui::Indent( static_cast<float>( depth ) * 14.0f );
            }

            const bool isSelected = ( gid == m_selectedGeomId );
            if ( ImGui::Selectable( label.c_str(), isSelected, ImGuiSelectableFlags_None ) )
            {
                m_selectedGeomId = gid;
                fprintf( stderr, "[VSP_VR] Geom browser: selected %s (%s)\n", gid.c_str(), dispName.c_str() );
            }

            if ( depth > 0 )
            {
                ImGui::Unindent( static_cast<float>( depth ) * 14.0f );
            }
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted( "Selected:" );
    if ( m_selectedGeomId.empty() )
    {
        ImGui::TextDisabled( "(none)" );
    }
    else
    {
        const std::string selType = vsp::GetGeomTypeName( m_selectedGeomId );
        const std::string selName = vsp::GetGeomName( m_selectedGeomId );
        const std::string selLine = selType + "  |  " + selName + "  |  " + m_selectedGeomId;
        ImGui::TextWrapped( "%s", selLine.c_str() );
    }

    ImGui::End();
}

void VRImGuiOverlay::RenderUiToTexture()
{
    if ( !m_ready )
    {
        return;
    }

    ImGui::Render();

    GLint prevFbo = 0;
    GLint prevViewport[4]{};
    glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prevFbo );
    glGetIntegerv( GL_VIEWPORT, prevViewport );

    glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
    glViewport( 0, 0, m_fbW, m_fbH );
    glClearColor( 0.06f, 0.06f, 0.08f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );

    glBindFramebuffer( GL_FRAMEBUFFER, static_cast<GLuint>( prevFbo ) );
    glViewport( prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3] );
}

void VRImGuiOverlay::DrawStageBillboard( const glm::mat4 &viewProj ) const
{
    if ( !m_ready )
    {
        return;
    }

    const float halfW = 0.48f;
    const float halfH = 0.36f;
    const float centerY = 1.28f;
    const float distZ = -1.05f;

    glm::mat4 model = glm::translate( glm::mat4( 1.0f ), glm::vec3( 0.0f, centerY, distZ ) );
    model = glm::scale( model, glm::vec3( halfW, halfH, 1.0f ) );

    const glm::mat4 mvp = viewProj * model;

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv( GL_DEPTH_WRITEMASK, &depthMask );
    glDepthMask( GL_FALSE );

    glUseProgram( m_billboardProgram );
    glUniformMatrix4fv( m_uBillboardMVP, 1, GL_FALSE, glm::value_ptr( mvp ) );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, m_colorTex );

    glBindVertexArray( m_billboardVAO );
    glDrawArrays( GL_TRIANGLES, 0, 6 );
    glBindVertexArray( 0 );

    glBindTexture( GL_TEXTURE_2D, 0 );
    glUseProgram( 0 );

    glDepthMask( depthMask );
    glDisable( GL_BLEND );
}
