find_package(FFMPEG REQUIRED)
find_package(draco CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(OpenVolumetricFFmpeg INTERFACE)
target_include_directories(OpenVolumetricFFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(OpenVolumetricFFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(OpenVolumetricFFmpeg INTERFACE
  ${FFMPEG_LIBRARIES}
)

add_library(OpenVolumetricCore STATIC
  adaptive/AdaptiveManifest.cpp
  adaptive/AdaptiveSelection.cpp
  container/FFmpegMp4VolumetricContainer.cpp
  container/GeometryPacket.cpp
  decoding/AdaptivePlayerCoordinator.cpp
  decoding/OpenVolumetricPlayer.cpp
  geometry/DracoPointCloudDecoder.cpp
  geometry/GeometryDecoderDraco.cpp
  io/FragmentedMp4Index.cpp
  io/HttpRangeByteSource.cpp
  io/LocalFileByteSource.cpp
  media/AVDecoderFFMPEG.cpp
  support/Logger.cpp
)

set_target_properties(OpenVolumetricCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(OpenVolumetricCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/decoding"
    "${CMAKE_CURRENT_SOURCE_DIR}/adaptive"
    "${CMAKE_CURRENT_SOURCE_DIR}/container"
    "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_link_libraries(OpenVolumetricCore
  PUBLIC
    OpenVolumetricBuildOptions
    OpenVolumetricFFmpeg
    CURL::libcurl
    draco::draco
    nlohmann_json::nlohmann_json
)
