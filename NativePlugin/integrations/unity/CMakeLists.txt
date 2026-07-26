set(VV_UNITY_API_SOURCES
  src/api/unity-volumetric-video-api.cpp
)

set(VV_UNITY_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
)

if(WIN32)
  add_library(VolumetricVideoNativePlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/d3d11/MeshBufferD3D11.cpp
    src/rendering/d3d11/TextureD3D11.cpp
    src/rendering/d3d11/VolumetricVideoD3D11.cpp
  )

  target_include_directories(VolumetricVideoNativePlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d11"
  )

  target_compile_definitions(VolumetricVideoNativePlugin
    PRIVATE
      NATIVEPLUGIN_EXPORTS
  )

  target_link_libraries(VolumetricVideoNativePlugin
    PRIVATE
      VolumetricVideoCore
      d3d11
  )

  set_target_properties(VolumetricVideoNativePlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/x64"
  )
elseif(APPLE)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

  add_library(VolumetricVideoNativePlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/metal/MeshBufferMetal.mm
    src/rendering/metal/TextureMetal.mm
    src/rendering/metal/VolumetricVideoMetal.cpp
  )

  set_source_files_properties(
    src/api/unity-volumetric-video-api.cpp
    PROPERTIES LANGUAGE OBJCXX
  )

  target_include_directories(VolumetricVideoNativePlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/metal"
  )

  target_link_libraries(VolumetricVideoNativePlugin
    PRIVATE
      VolumetricVideoCore
      ${METAL_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
  )

  target_compile_options(VolumetricVideoNativePlugin
    PRIVATE
      "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
  )

  set_target_properties(VolumetricVideoNativePlugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/VolumetricVideo/macOS"
  )
endif()
