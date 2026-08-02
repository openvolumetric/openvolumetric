cmake_minimum_required(VERSION 3.20)

project(OpenVolumetricNative LANGUAGES C CXX)

include(CTest)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENVOLUMETRIC_WARNINGS_AS_ERRORS
  "Treat OpenVolumetric compiler warnings as errors" OFF)
option(OPENVOLUMETRIC_ENABLE_SANITIZERS
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(OpenVolumetricBuildOptions INTERFACE)
if(MSVC)
  target_compile_options(OpenVolumetricBuildOptions INTERFACE /W4)
  if(OPENVOLUMETRIC_WARNINGS_AS_ERRORS)
    target_compile_options(OpenVolumetricBuildOptions INTERFACE /WX)
  endif()
else()
  target_compile_options(OpenVolumetricBuildOptions INTERFACE
    -Wall -Wextra -Wpedantic)
  if(OPENVOLUMETRIC_WARNINGS_AS_ERRORS)
    target_compile_options(OpenVolumetricBuildOptions INTERFACE -Werror)
  endif()
  if(OPENVOLUMETRIC_ENABLE_SANITIZERS)
    target_compile_options(OpenVolumetricBuildOptions INTERFACE
      -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(OpenVolumetricBuildOptions INTERFACE
      -fno-omit-frame-pointer -fsanitize=address,undefined)
  endif()
endif()

add_subdirectory(src/core)
if(NOT ANDROID)
  add_subdirectory(src/authoring)
endif()

# The native Unity integration is built here. Unreal links the same portable
# core from its engine module rather than compiling an additional CMake target.
if(WIN32 OR APPLE OR ANDROID)
  add_subdirectory(integrations/unity)
else()
  message(STATUS
    "Building OpenVolumetricCore only; no engine integration is available for this platform")
endif()

if(BUILD_TESTING AND NOT ANDROID)
  add_subdirectory(tests)
endif()
