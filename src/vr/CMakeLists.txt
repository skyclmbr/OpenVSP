add_library( vsp_vr STATIC
    VRInterfaces.cpp
    VRMode.cpp
)

target_compile_definitions( vsp_vr PUBLIC VSP_VR_ENABLED )
target_include_directories( vsp_vr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
