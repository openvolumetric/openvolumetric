set(VV_UNITY_API_SOURCES
  src/api/openvolumetric-unity-api.cpp
  src/api/UnityOpenVolumetricPlayer.cpp
  src/audio/OpenVolumetricAudioPlugin.cpp
  src/audio/UnityAudioBridge.cpp
)

set(VV_UNITY_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/common"
)

# Treat the vendored Unity SDK as external so strict warning builds report
# OpenVolumetric issues without requiring edits to upstream headers.
set(VV_UNITY_SYSTEM_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity_native_plugin_sdk/include"
)

if(WIN32)
  add_library(OpenVolumetricUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/d3d11/MeshBufferD3D11.cpp
    src/rendering/d3d11/TextureD3D11.cpp
    src/rendering/d3d12/MeshBufferD3D12.cpp
    src/rendering/d3d12/TextureD3D12.cpp
  )

  target_include_directories(OpenVolumetricUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d11"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d12"
  )
  target_include_directories(OpenVolumetricUnityPlugin SYSTEM PRIVATE
    "${VV_UNITY_SYSTEM_INCLUDE_DIR}"
  )

  target_link_libraries(OpenVolumetricUnityPlugin
    PRIVATE
      OpenVolumetricCore
      d3d11
      d3d12
  )

  set_target_properties(OpenVolumetricUnityPlugin PROPERTIES
    OUTPUT_NAME "AudioPluginOpenVolumetricUnity"
    RUNTIME_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/x64"
  )
elseif(APPLE)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

  add_library(OpenVolumetricUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/metal/MeshBufferMetal.mm
    src/rendering/metal/TextureMetal.mm
  )

  set_source_files_properties(
    src/api/openvolumetric-unity-api.cpp
    PROPERTIES LANGUAGE OBJCXX
  )

  target_include_directories(OpenVolumetricUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/metal"
  )
  target_include_directories(OpenVolumetricUnityPlugin SYSTEM PRIVATE
    "${VV_UNITY_SYSTEM_INCLUDE_DIR}"
  )

  target_link_libraries(OpenVolumetricUnityPlugin
    PRIVATE
      OpenVolumetricCore
      ${METAL_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
  )

  target_compile_options(OpenVolumetricUnityPlugin
    PRIVATE
      "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
  )

  set_target_properties(OpenVolumetricUnityPlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "AudioPluginOpenVolumetricUnity"
    LIBRARY_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/macOS"
  )

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/AudioPluginOpenVolumetricUnity.dylib.meta.template"
    "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/macOS/AudioPluginOpenVolumetricUnity.dylib.meta"
    COPYONLY
  )
elseif(ANDROID)
  find_library(VULKAN_LIBRARY vulkan REQUIRED)
  find_library(LOG_LIBRARY log REQUIRED)

  add_library(OpenVolumetricUnityPlugin SHARED
    ${VV_UNITY_API_SOURCES}
    src/rendering/vulkan/MeshBufferVulkan.cpp
    src/rendering/vulkan/TextureVulkan.cpp
    src/rendering/vulkan/VulkanUploadBuffer.cpp
  )

  target_include_directories(OpenVolumetricUnityPlugin
    PRIVATE
      ${VV_UNITY_INCLUDE_DIRS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/vulkan"
  )
  target_include_directories(OpenVolumetricUnityPlugin SYSTEM PRIVATE
    "${VV_UNITY_SYSTEM_INCLUDE_DIR}"
  )

  target_link_libraries(OpenVolumetricUnityPlugin
    PRIVATE
      OpenVolumetricCore
      ${VULKAN_LIBRARY}
      ${LOG_LIBRARY}
  )

  target_link_options(OpenVolumetricUnityPlugin
    PRIVATE
      "-Wl,-Bsymbolic"
      "-Wl,-z,max-page-size=16384"
      "-Wl,-z,common-page-size=16384"
  )

  set_target_properties(OpenVolumetricUnityPlugin PROPERTIES
    OUTPUT_NAME "AudioPluginOpenVolumetricUnity"
    LIBRARY_OUTPUT_DIRECTORY
      "${PROJECT_SOURCE_DIR}/../Unity/Assets/Plugins/OpenVolumetric/Android/arm64-v8a"
  )

  add_custom_command(TARGET OpenVolumetricUnityPlugin POST_BUILD
    COMMAND "${CMAKE_STRIP}" --strip-unneeded
      "$<TARGET_FILE:OpenVolumetricUnityPlugin>"
    COMMENT "Stripping Android native plugin"
  )
endif()

openvolumetric_apply_build_options(OpenVolumetricUnityPlugin)
