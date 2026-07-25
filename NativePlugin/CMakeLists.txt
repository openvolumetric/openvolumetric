cmake_minimum_required(VERSION 3.20)

project(VolumetricVideoNativePlugin LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FFMPEG REQUIRED)
find_package(draco CONFIG REQUIRED)
find_library(VV_SWRESAMPLE_LIBRARY
  NAMES swresample
  PATHS ${FFMPEG_LIBRARY_DIRS}
  REQUIRED
)

add_library(VolumetricVideoFFmpeg INTERFACE)
target_include_directories(VolumetricVideoFFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(VolumetricVideoFFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VolumetricVideoFFmpeg INTERFACE
  ${FFMPEG_LIBRARIES}
  ${VV_SWRESAMPLE_LIBRARY}
)

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
)
target_link_libraries(VolumetricVideoCore
  PUBLIC
    VolumetricVideoFFmpeg
    draco::draco
)

# Build the Unity rendering bridge for the native graphics API on each
# supported desktop platform. Other platforms can still build the decoder core.
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
elseif(APPLE)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

  add_library(VolumetricVideoNativePlugin SHARED
    src/MeshBufferMetal.mm
    src/TextureMetal.mm
    src/VolumetricVideoMetal.cpp
    src/unity-volumetric-video-api.cpp
  )
  set_source_files_properties(
    src/unity-volumetric-video-api.cpp
    PROPERTIES LANGUAGE OBJCXX
  )
  target_include_directories(VolumetricVideoNativePlugin
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(VolumetricVideoNativePlugin
    PRIVATE
      VolumetricVideoCore
      ${METAL_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
  )
  target_compile_options(VolumetricVideoNativePlugin
    PRIVATE
      "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
  )
  set_target_properties(VolumetricVideoNativePlugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/macOS"
  )
else()
  message(STATUS
    "Building VolumetricVideoCore only; no Unity rendering bridge is available for this platform")
endif()
