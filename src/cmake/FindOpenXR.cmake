# FindOpenXR — locate Khronos OpenXR loader and headers (Windows / cross-platform).
#
# Sets:
#   OpenXR_FOUND
#   OpenXR_INCLUDE_DIR
#   OpenXR_LOADER_LIBRARY
# Target:
#   OpenXR::openxr_loader
#
# Hints: OpenXR_ROOT, CMAKE_PREFIX_PATH, env OpenXR_SDK / OPENXR_SDK

find_path( OpenXR_INCLUDE_DIR
    NAMES openxr/openxr.h
    HINTS
        "${OpenXR_ROOT}"
        "$ENV{OpenXR_SDK}"
        "$ENV{OPENXR_SDK}"
    PATH_SUFFIXES include
)

find_library( OpenXR_LOADER_LIBRARY
    NAMES openxr_loader
    HINTS
        "${OpenXR_ROOT}"
        "$ENV{OpenXR_SDK}"
        "$ENV{OPENXR_SDK}"
    PATH_SUFFIXES lib Lib/x64 lib/x64
)

include( FindPackageHandleStandardArgs )
find_package_handle_standard_args( OpenXR DEFAULT_MSG OpenXR_INCLUDE_DIR OpenXR_LOADER_LIBRARY )

if( OpenXR_FOUND )
    add_library( OpenXR::openxr_loader UNKNOWN IMPORTED GLOBAL )
    set_target_properties( OpenXR::openxr_loader PROPERTIES
        IMPORTED_LOCATION "${OpenXR_LOADER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OpenXR_INCLUDE_DIR}"
    )
endif()
