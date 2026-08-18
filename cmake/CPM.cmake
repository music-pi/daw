# CPM.cmake - minimal package manager for CMake
# https://github.com/cpm-cmake/CPM.cmake
# Pinned lightweight bootstrap
if(DEFINED CPM_DIRECTORY)
  set(CPM_SOURCE_CACHE ${CPM_DIRECTORY}/cpm-cache)
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_SOURCE_CACHE $ENV{CPM_SOURCE_CACHE})
else()
  set(CPM_SOURCE_CACHE "${CMAKE_BINARY_DIR}/cpm-cache")
endif()

if(NOT CPM_VERSION)
  set(CPM_VERSION 0.38.1)
endif()

set(CPM_DOWNLOAD_VERSION ${CPM_VERSION})
set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    ${CPM_DOWNLOAD_LOCATION}
    STATUS status
  )
  list(GET status 0 status_code)
  if(NOT status_code EQUAL 0)
    message(FATAL_ERROR "CPM.cmake download failed with status: ${status}")
  endif()
endif()

include(${CPM_DOWNLOAD_LOCATION})
