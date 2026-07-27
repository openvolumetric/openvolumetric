add_library(OpenVolAuthoringCore STATIC
  DracoMeshEncoder.cpp
  VolumetricVideoPacker.cpp
)

set_target_properties(OpenVolAuthoringCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(OpenVolAuthoringCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpenVolAuthoringCore
  PUBLIC
    OpenVolCore
)

if(WIN32 OR APPLE)
  add_library(OpenVolAuthoring SHARED
    OpenVolAuthoringApi.cpp
  )

  target_link_libraries(OpenVolAuthoring
    PRIVATE
      OpenVolAuthoringCore
  )

  if(WIN32)
    target_compile_definitions(OpenVolAuthoring
      PRIVATE
        OPENVOL_AUTHORING_EXPORTS
    )
    set_target_properties(OpenVolAuthoring PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVol/x64"
    )
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/OpenVolAuthoring.dll.meta"
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVol/x64/OpenVolAuthoring.dll.meta"
      COPYONLY
    )
  elseif(APPLE)
    set_target_properties(OpenVolAuthoring PROPERTIES
      PREFIX ""
      LIBRARY_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVol/macOS"
    )
  endif()
endif()
