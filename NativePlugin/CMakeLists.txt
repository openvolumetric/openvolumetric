cmake_minimum_required(VERSION 3.20)

project(VolumetricVideoNativePlugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Draco 1.3 relies on an indirect <cstdint> include that newer GCC versions
# no longer provide. Keep the pinned submodule untouched while supplying it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:cstdint>")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavcodec
  libavformat
  libavutil
)

# Draco is pinned by the repository submodule and already has a CMake build.
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_UNITY_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_MAYA_PLUGIN OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
set(DRACO_TRANSCODER_SUPPORTED OFF CACHE BOOL "" FORCE)
add_subdirectory(draco EXCLUDE_FROM_ALL)

if(TARGET draco)
  set(VV_DRACO_TARGET draco)
elseif(TARGET draco_static)
  set(VV_DRACO_TARGET draco_static)
else()
  message(FATAL_ERROR "The pinned Draco source did not create a supported library target")
endif()

set(VV_CORE_SOURCES
  src/AVDecoderFFMPEG.cpp
  src/GeometryDecoderDraco.cpp
  src/IAVDecoder.cpp
  src/IDecoder.cpp
  src/IGeometryDecoder.cpp
  src/IMeshBuffer.cpp
  src/ITexture.cpp
  src/IVolumetricVideo.cpp
  src/Logger.cpp
  src/Mesh.cpp
  src/Stream.cpp
)

add_library(VolumetricVideoCore STATIC ${VV_CORE_SOURCES})
set_target_properties(VolumetricVideoCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(VolumetricVideoCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/draco/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(VolumetricVideoCore
  PUBLIC
    PkgConfig::FFMPEG
    ${VV_DRACO_TARGET}
)

# The current Unity rendering bridge is D3D11-only. Keeping it conditional
# allows the portable decoder core and its dependencies to build elsewhere.
if(WIN32)
  add_library(VolumetricVideoNativePlugin SHARED
    src/MeshBufferD3D11.cpp
    src/TextureD3D11.cpp
    src/VolumetricVideoD3D11.cpp
    src/unity-volumetric-video-api.cpp
  )
  target_include_directories(VolumetricVideoNativePlugin
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_definitions(VolumetricVideoNativePlugin
    PRIVATE
      NATIVEPLUGIN_EXPORTS
  )
  target_link_libraries(VolumetricVideoNativePlugin
    PRIVATE
      VolumetricVideoCore
      d3d11
  )
  set_target_properties(VolumetricVideoNativePlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/x64"
  )
else()
  message(STATUS
    "Building VolumetricVideoCore only; the Unity rendering bridge currently requires Windows/D3D11")
endif()
