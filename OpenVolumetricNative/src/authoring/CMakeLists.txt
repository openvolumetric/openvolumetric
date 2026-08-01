add_library(OpenVolumetricAuthoringCore STATIC
  AdaptivePackage.cpp
  AuthoringWorkflow.cpp
  DracoMeshEncoder.cpp
  DracoPointCloudEncoder.cpp
  TopologyAnalyzer.cpp
  VolumetricVideoPacker.cpp
)

set_target_properties(OpenVolumetricAuthoringCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(OpenVolumetricAuthoringCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpenVolumetricAuthoringCore
  PUBLIC
    OpenVolumetricCore
)

if(WIN32 OR APPLE)
  add_library(OpenVolumetricAuthoring SHARED
    OpenVolumetricAuthoringApi.cpp
  )

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
