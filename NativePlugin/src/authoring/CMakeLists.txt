add_library(VolumetricVideoAuthoringCore STATIC
  DracoMeshEncoder.cpp
  VolumetricVideoPacker.cpp
)

set_target_properties(VolumetricVideoAuthoringCore PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(VolumetricVideoAuthoringCore
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(VolumetricVideoAuthoringCore
  PUBLIC
    VolumetricVideoCore
)

if(WIN32 OR APPLE)
  add_library(VolumetricVideoAuthoring SHARED
    VolumetricVideoAuthoringApi.cpp
  )

  target_link_libraries(VolumetricVideoAuthoring
    PRIVATE
      VolumetricVideoAuthoringCore
  )

  if(WIN32)
    target_compile_definitions(VolumetricVideoAuthoring
      PRIVATE
        VOLUMETRIC_VIDEO_AUTHORING_EXPORTS
    )
    set_target_properties(VolumetricVideoAuthoring PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/x64"
    )
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/VolumetricVideoAuthoring.dll.meta"
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/x64/VolumetricVideoAuthoring.dll.meta"
      COPYONLY
    )
  elseif(APPLE)
    set_target_properties(VolumetricVideoAuthoring PROPERTIES
      PREFIX ""
      LIBRARY_OUTPUT_DIRECTORY
        "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/macOS"
    )
  endif()
endif()
