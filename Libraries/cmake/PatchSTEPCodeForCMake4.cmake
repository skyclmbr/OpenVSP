# Patches extracted STEPCODE sources for CMake 4.x (invoked from External_STEPCode.cmake).
if(NOT DEFINED STEP_SOURCE)
  message(FATAL_ERROR "PatchSTEPCodeForCMake4.cmake requires -DSTEP_SOURCE=<path>")
endif()

set(_main "${STEP_SOURCE}/CMakeLists.txt")
if(NOT EXISTS "${_main}")
  message(FATAL_ERROR "Missing ${_main}")
endif()

file(READ "${_main}" _main_txt)
string(REPLACE
  [[cmake_minimum_required(VERSION 2.8.7)
if(COMMAND CMAKE_POLICY)
  CMAKE_POLICY(SET CMP0003 NEW)
  if ("${CMAKE_VERSION}" VERSION_GREATER 2.99)
    CMAKE_POLICY(SET CMP0026 OLD)
  endif ("${CMAKE_VERSION}" VERSION_GREATER 2.99)
endif(COMMAND CMAKE_POLICY)]]
  [[cmake_minimum_required(VERSION 3.5)
if(COMMAND CMAKE_POLICY)
  CMAKE_POLICY(SET CMP0003 NEW)
endif(COMMAND CMAKE_POLICY)]]
  _main_txt
  "${_main_txt}"
)
# Fallback if line endings differ (CRLF archives).
string(REPLACE
  [[cmake_minimum_required(VERSION 2.8.7)
if(COMMAND CMAKE_POLICY)
  CMAKE_POLICY(SET CMP0003 NEW)
  if ("${CMAKE_VERSION}" VERSION_GREATER 2.99)
    CMAKE_POLICY(SET CMP0026 OLD)
  endif ("${CMAKE_VERSION}" VERSION_GREATER 2.99)
endif(COMMAND CMAKE_POLICY)]]
  [[cmake_minimum_required(VERSION 3.5)
if(COMMAND CMAKE_POLICY)
  CMAKE_POLICY(SET CMP0003 NEW)
endif(COMMAND CMAKE_POLICY)]]
  _main_txt
  "${_main_txt}"
)
file(WRITE "${_main}" "${_main_txt}")

set(_scan "${STEP_SOURCE}/cmake/schema_scanner/CMakeLists.txt")
if(NOT EXISTS "${_scan}")
  message(FATAL_ERROR "Missing ${_scan}")
endif()

file(READ "${_scan}" _scan_txt)
string(REPLACE
  [[project(SC_SUBPROJECT_SCHEMA_SCANNER)
cmake_minimum_required(VERSION 2.8.7)]]
  [[cmake_minimum_required(VERSION 3.5)
project(SC_SUBPROJECT_SCHEMA_SCANNER)]]
  _scan_txt
  "${_scan_txt}"
)
string(REPLACE
  [[project(SC_SUBPROJECT_SCHEMA_SCANNER)
cmake_minimum_required(VERSION 2.8.7)]]
  [[cmake_minimum_required(VERSION 3.5)
project(SC_SUBPROJECT_SCHEMA_SCANNER)]]
  _scan_txt
  "${_scan_txt}"
)
file(WRITE "${_scan}" "${_scan_txt}")
