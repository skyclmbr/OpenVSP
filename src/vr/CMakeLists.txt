# OpenXR: default to FetchContent (Khronos OpenXR-SDK) so VSP_VR=ON works without a manual SDK install.
# To use an existing SDK instead: -DVSP_VR_SYSTEM_OPENXR=ON and set CMAKE_PREFIX_PATH / OpenXR_ROOT.

option( VSP_VR_SYSTEM_OPENXR "Use FindOpenXR (system-installed SDK) instead of FetchContent" OFF )

if( VSP_VR_SYSTEM_OPENXR )
    find_package( OpenXR REQUIRED )
    set( VSP_VR_OPENXR_TARGET OpenXR::openxr_loader )
else()
    set( _VSP_SAVED_BUILD_TESTING "${BUILD_TESTING}" )
    set( BUILD_TESTING OFF CACHE BOOL "" FORCE )

    include( FetchContent )
    FetchContent_Declare(
        KhronosOpenXRSDK
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG        release-1.0.34
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable( KhronosOpenXRSDK )

    if( NOT TARGET openxr_loader )
        message( FATAL_ERROR "OpenXR FetchContent did not produce target openxr_loader" )
    endif()
    set( VSP_VR_OPENXR_TARGET openxr_loader )

    if( DEFINED _VSP_SAVED_BUILD_TESTING )
        set( BUILD_TESTING "${_VSP_SAVED_BUILD_TESTING}" CACHE BOOL "" FORCE )
    endif()
endif()

add_library( vsp_vr STATIC
    VRInterfaces.cpp
    VRMode.cpp
    VRManager.cpp
)

target_compile_definitions( vsp_vr PUBLIC VSP_VR_ENABLED )

target_include_directories( vsp_vr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )

target_include_directories( vsp_vr PRIVATE ${GLM_INCLUDE_DIR} ${GLEW_INCLUDE_PATH} )

target_link_libraries( vsp_vr
    PUBLIC
        ${VSP_VR_OPENXR_TARGET}
    PRIVATE
        ${GLEW_LIBRARY}
)

if( WIN32 )
    target_link_libraries( vsp_vr PRIVATE opengl32 user32 gdi32 )
endif()
