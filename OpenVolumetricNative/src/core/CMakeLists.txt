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
  adaptive/AdaptivePolicy.cpp
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
add_library(OpenVolumetric::Core ALIAS OpenVolumetricCore)

set_target_properties(OpenVolumetricCore PROPERTIES
  EXPORT_NAME Core
  POSITION_INDEPENDENT_CODE ON
)
openvolumetric_apply_build_options(OpenVolumetricCore)

target_include_directories(OpenVolumetricCore
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/decoding>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adaptive>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/container>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/geometry>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/io>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/media>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/OpenVolumetric>"
)

target_link_libraries(OpenVolumetricCore
  PUBLIC
    "$<BUILD_INTERFACE:OpenVolumetricFFmpeg>"
    "$<INSTALL_INTERFACE:OpenVolumetric::FFmpeg>"
    CURL::libcurl
    draco::draco
    nlohmann_json::nlohmann_json
)

# Compile the public contracts without implementation or engine include paths.
# This target catches accidental Unity, Unreal, FFmpeg, Draco, or concrete
# transport dependencies at the engine-neutral boundary.
add_library(OpenVolumetricCoreHeaderCheck OBJECT
  "${PROJECT_SOURCE_DIR}/tests/CoreHeaderCheck.cpp"
)
target_include_directories(OpenVolumetricCoreHeaderCheck PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/decoding"
  "${CMAKE_CURRENT_SOURCE_DIR}/adaptive"
  "${CMAKE_CURRENT_SOURCE_DIR}/container"
  "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
  "${CMAKE_CURRENT_SOURCE_DIR}/io"
  "${CMAKE_CURRENT_SOURCE_DIR}/media"
)
openvolumetric_apply_build_options(OpenVolumetricCoreHeaderCheck)

# Only engine-neutral façades and contracts form the installed SDK. Concrete
# FFmpeg, Draco, transport, queue, and fragmented-index headers remain private.
set(OPENVOLUMETRIC_PUBLIC_HEADERS
  adaptive/AdaptiveManifest.h
  adaptive/AdaptivePolicy.h
  adaptive/AdaptiveSelection.h
  container/GeometryPacket.h
  container/IVolumetricContainer.h
  container/TimedFrame.h
  decoding/AdaptivePlayerCoordinator.h
  decoding/IAVDecoder.h
  decoding/IDecoder.h
  decoding/IGeometryDecoder.h
  decoding/OpenVolumetricPlayer.h
  geometry/Mesh.h
  io/IByteSource.h
  media/Stream.h
  support/Logger.h
)

install(TARGETS
  OpenVolumetricCore
  EXPORT OpenVolumetricTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES ${OPENVOLUMETRIC_PUBLIC_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/OpenVolumetric"
)
