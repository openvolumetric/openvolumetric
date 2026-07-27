set(VV_UNITY_API_SOURCES
  src/api/openvol-unity-api.cpp
)

set(VV_UNITY_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
)

if(WIN32)
  add_library(OpenVolUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/d3d11/MeshBufferD3D11.cpp
    src/rendering/d3d11/TextureD3D11.cpp
    src/rendering/d3d11/VolumetricVideoD3D11.cpp
  )

  target_include_directories(OpenVolUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d11"
  )

  target_compile_definitions(OpenVolUnityPlugin
    PRIVATE
      NATIVEPLUGIN_EXPORTS
  )

  target_link_libraries(OpenVolUnityPlugin
    PRIVATE
      OpenVolCore
      d3d11
  )

  set_target_properties(OpenVolUnityPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVol/x64"
  )
elseif(APPLE)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

  add_library(OpenVolUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/metal/MeshBufferMetal.mm
    src/rendering/metal/TextureMetal.mm
    src/rendering/metal/VolumetricVideoMetal.cpp
  )

  set_source_files_properties(
    src/api/openvol-unity-api.cpp
    PROPERTIES LANGUAGE OBJCXX
  )

  target_include_directories(OpenVolUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/metal"
  )

  target_link_libraries(OpenVolUnityPlugin
    PRIVATE
      OpenVolCore
      ${METAL_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
  )

  target_compile_options(OpenVolUnityPlugin
    PRIVATE
      "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
  )

  set_target_properties(OpenVolUnityPlugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVol/macOS"
  )
elseif(ANDROID)
  find_library(VULKAN_LIBRARY vulkan REQUIRED)
  find_library(LOG_LIBRARY log REQUIRED)

  add_library(OpenVolUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/vulkan/MeshBufferVulkan.cpp
    src/rendering/vulkan/TextureVulkan.cpp
    src/rendering/vulkan/VulkanUploadBuffer.cpp
    src/rendering/vulkan/VolumetricVideoVulkan.cpp
  )

  target_include_directories(OpenVolUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/vulkan"
  )

  target_compile_definitions(OpenVolUnityPlugin
    PRIVATE
      NATIVEPLUGIN_EXPORTS
  )

  target_link_libraries(OpenVolUnityPlugin
    PRIVATE
      OpenVolCore
      ${VULKAN_LIBRARY}
      ${LOG_LIBRARY}
  )

  target_link_options(OpenVolUnityPlugin
    PRIVATE
      "-Wl,-Bsymbolic"
  )

  set_target_properties(OpenVolUnityPlugin PROPERTIES
    OUTPUT_NAME "OpenVolUnityPlugin"
    LIBRARY_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/Android/arm64-v8a"
  )

  add_custom_command(TARGET OpenVolUnityPlugin POST_BUILD
    COMMAND "${CMAKE_STRIP}" --strip-unneeded
      "$<TARGET_FILE:OpenVolUnityPlugin>"
    COMMENT "Stripping Android native plugin"
  )
endif()
