# Nuke exports its NDK as a CMake package from inside the application
# installation. Accept the install directory, application bundle, CMake
# package directory, or a normal CMAKE_PREFIX_PATH entry so local paths never
# need to be committed.
if(OPENVOLUMETRIC_NUKE_ROOT AND NOT Nuke_DIR)
  file(GLOB _openvolumetric_nuke_app_cmake_dirs
    "${OPENVOLUMETRIC_NUKE_ROOT}/Nuke*.app/Contents/MacOS/cmake"
  )
  set(_openvolumetric_nuke_cmake_candidates
    "${OPENVOLUMETRIC_NUKE_ROOT}"
    "${OPENVOLUMETRIC_NUKE_ROOT}/cmake"
    "${OPENVOLUMETRIC_NUKE_ROOT}/Contents/MacOS/cmake"
    ${_openvolumetric_nuke_app_cmake_dirs}
  )
  find_path(Nuke_DIR
    NAMES NukeConfig.cmake
    PATHS ${_openvolumetric_nuke_cmake_candidates}
    NO_DEFAULT_PATH
  )
endif()

find_package(Nuke REQUIRED CONFIG)

add_nuke_plugin(OpenVolumetricGeo
  src/NukeReaderSession.cpp
  src/OpenVolumetricGeo.cpp
  src/OpenVolumetricTexture.cpp
)
target_link_libraries(OpenVolumetricGeo PRIVATE
  OpenVolumetric::Core
)
openvolumetric_apply_build_options(OpenVolumetricGeo)

set_target_properties(OpenVolumetricGeo PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
)

# Nuke does not eagerly load every binary found on NUKE_PATH. Register a menu
# command so creating the node causes Nuke to load the matching plug-in module.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/python/menu.py"
  "${CMAKE_CURRENT_BINARY_DIR}/plugins/menu.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/python/openvolumetric_read.py"
  "${CMAKE_CURRENT_BINARY_DIR}/plugins/openvolumetric_read.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/python/init.py"
  "${CMAKE_CURRENT_BINARY_DIR}/plugins/init.py"
  COPYONLY
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/icons")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/icons/OpenVolumetric.png"
  "${CMAKE_CURRENT_BINARY_DIR}/plugins/icons/OpenVolumetric.png"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/plugins/OpenVolumetricTexture.tcl"
  "${CMAKE_CURRENT_BINARY_DIR}/plugins/OpenVolumetricTexture.tcl"
  COPYONLY
)
