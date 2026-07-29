find_package(FFMPEG REQUIRED)
find_package(draco CONFIG REQUIRED)

find_library(VV_SWRESAMPLE_LIBRARY
  NAMES swresample
  PATHS ${FFMPEG_LIBRARY_DIRS}
  REQUIRED
)

add_library(OpenVolumetricFFmpeg INTERFACE)
target_include_directories(OpenVolumetricFFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(OpenVolumetricFFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(OpenVolumetricFFmpeg INTERFACE
  ${FFMPEG_LIBRARIES}
  ${VV_SWRESAMPLE_LIBRARY}
)

add_library(OpenVolumetricCore STATIC
  container/FFmpegMp4VolumetricContainer.cpp
  container/GeometryPacket.cpp
  decoding/OpenVolumetricPlayer.cpp
  geometry/DracoPointCloudDecoder.cpp
  geometry/GeometryDecoderDraco.cpp
  media/AVDecoderFFMPEG.cpp
  support/Logger.cpp
)

set_target_properties(OpenVolumetricCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(OpenVolumetricCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/decoding"
    "${CMAKE_CURRENT_SOURCE_DIR}/container"
    "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_link_libraries(OpenVolumetricCore
  PUBLIC
    OpenVolumetricFFmpeg
    draco::draco
)
