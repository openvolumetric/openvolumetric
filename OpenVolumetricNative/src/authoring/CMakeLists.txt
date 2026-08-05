add_library(OpenVolumetricAuthoringCore STATIC
  AdaptivePackage.cpp
  AuthoringWorkflow.cpp
  DracoMeshEncoder.cpp
  DracoPointCloudEncoder.cpp
  TopologyAnalyzer.cpp
  VolumetricVideoPacker.cpp
)
add_library(OpenVolumetric::Authoring ALIAS OpenVolumetricAuthoringCore)

set_target_properties(OpenVolumetricAuthoringCore PROPERTIES
  EXPORT_NAME Authoring
  POSITION_INDEPENDENT_CODE ON
)
openvolumetric_apply_build_options(OpenVolumetricAuthoringCore)

target_include_directories(OpenVolumetricAuthoringCore
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/OpenVolumetric>"
)

target_link_libraries(OpenVolumetricAuthoringCore
  PUBLIC
    OpenVolumetricCore
)

if(WIN32 OR APPLE)
  add_library(OpenVolumetricAuthoring SHARED
    OpenVolumetricAuthoringApi.cpp
  )
  openvolumetric_apply_build_options(OpenVolumetricAuthoring)

  target_link_libraries(OpenVolumetricAuthoring
    PRIVATE
      OpenVolumetricAuthoringCore
  )

  if(WIN32)
    target_compile_definitions(OpenVolumetricAuthoring
      PRIVATE
        OPENVOLUMETRIC_AUTHORING_EXPORTS
    )
    set_target_properties(OpenVolumetricAuthoring PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/x64"
    )
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/OpenVolumetricAuthoring.dll.meta"
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/x64/OpenVolumetricAuthoring.dll.meta"
      COPYONLY
    )
  elseif(APPLE)
    set_target_properties(OpenVolumetricAuthoring PROPERTIES
      PREFIX ""
      LIBRARY_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/macOS"
    )
  endif()
endif()

install(TARGETS OpenVolumetricAuthoringCore
  EXPORT OpenVolumetricTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES
  AdaptivePackage.h
  AuthoringWorkflow.h
  DracoMeshEncoder.h
  VolumetricVideoPacker.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/OpenVolumetric"
)
