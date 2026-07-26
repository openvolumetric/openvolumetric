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

add_library(VolumetricVideoCore STATIC
  decoding/IAVDecoder.cpp
  decoding/IDecoder.cpp
  decoding/IGeometryDecoder.cpp
  decoding/IMeshBuffer.cpp
  decoding/ITexture.cpp
  decoding/IVolumetricVideo.cpp
  geometry/GeometryDecoderDraco.cpp
  geometry/Mesh.cpp
  media/AVDecoderFFMPEG.cpp
  media/Stream.cpp
  support/Logger.cpp
)

set_target_properties(VolumetricVideoCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(VolumetricVideoCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/decoding"
    "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_link_libraries(VolumetricVideoCore
  PUBLIC
    VolumetricVideoFFmpeg
    draco::draco
)
