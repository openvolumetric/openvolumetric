find_package(FFMPEG REQUIRED)
find_package(draco CONFIG REQUIRED)

find_library(VV_SWRESAMPLE_LIBRARY
  NAMES swresample
  PATHS ${FFMPEG_LIBRARY_DIRS}
  REQUIRED
)

add_library(OpenVolFFmpeg INTERFACE)
target_include_directories(OpenVolFFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(OpenVolFFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(OpenVolFFmpeg INTERFACE
  ${FFMPEG_LIBRARIES}
  ${VV_SWRESAMPLE_LIBRARY}
)

add_library(OpenVolCore STATIC
  container/FFmpegMp4VolumetricContainer.cpp
  container/GeometryPacket.cpp
  decoding/IVolumetricVideo.cpp
  decoding/OpenVolPlayer.cpp
  geometry/GeometryDecoderDraco.cpp
  media/AVDecoderFFMPEG.cpp
  support/Logger.cpp
)

set_target_properties(OpenVolCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(OpenVolCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/decoding"
    "${CMAKE_CURRENT_SOURCE_DIR}/container"
    "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_link_libraries(OpenVolCore
  PUBLIC
    OpenVolFFmpeg
    draco::draco
)
