cmake_minimum_required(VERSION 3.20)

project(OpenVolumetricNative LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/core)
if(NOT ANDROID)
  add_subdirectory(src/authoring)
endif()

# Engine integrations link to the portable decoder core. More integrations,
# such as Unreal, can be added alongside Unity without changing the core.
if(WIN32 OR APPLE OR ANDROID)
  add_subdirectory(integrations/unity)
else()
  message(STATUS
    "Building OpenVolumetricCore only; no engine integration is available for this platform")
endif()
