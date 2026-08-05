cmake_minimum_required(VERSION 3.20)

project(OpenVolumetricNative VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENVOLUMETRIC_WARNINGS_AS_ERRORS
  "Treat OpenVolumetric compiler warnings as errors" OFF)
option(OPENVOLUMETRIC_ENABLE_SANITIZERS
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(openvolumetric_apply_build_options target)
  if(WIN32)
    # Prevent Windows SDK min/max macros from breaking std::min/std::max calls.
    target_compile_definitions(${target} PRIVATE NOMINMAX)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(OPENVOLUMETRIC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(OPENVOLUMETRIC_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(OPENVOLUMETRIC_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=address,undefined)
      target_link_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

add_subdirectory(src/core)
if(NOT ANDROID)
  add_subdirectory(src/authoring)
endif()

# The native Unity integration is built here. Unreal links the same portable
# core from its engine module rather than compiling an additional CMake target.
if(WIN32 OR APPLE OR ANDROID)
  add_subdirectory(integrations/unity)
else()
  message(STATUS
    "Building OpenVolumetricCore only; no engine integration is available for this platform")
endif()

if(BUILD_TESTING AND NOT ANDROID)
  add_subdirectory(tests)
  # Values substituted into the CMake script must use forward slashes. A
  # backslash path such as C:\Program Files is otherwise parsed as escapes.
  file(TO_CMAKE_PATH "${CMAKE_MAKE_PROGRAM}"
    OPENVOLUMETRIC_CONSUMER_MAKE_PROGRAM)
  file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}"
    OPENVOLUMETRIC_CONSUMER_TOOLCHAIN_FILE)
  file(TO_CMAKE_PATH "${VCPKG_INSTALLED_DIR}"
    OPENVOLUMETRIC_CONSUMER_VCPKG_INSTALLED_DIR)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/InstalledConsumerTest.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InstalledConsumerTest.cmake"
    @ONLY
  )
  add_test(
    NAME OpenVolumetricInstalledConsumerTest
    COMMAND "${CMAKE_COMMAND}" -P
      "${CMAKE_CURRENT_BINARY_DIR}/InstalledConsumerTest.cmake"
  )
endif()

install(EXPORT OpenVolumetricTargets
  FILE OpenVolumetricTargets.cmake
  NAMESPACE OpenVolumetric::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenVolumetric"
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenVolumetricConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenVolumetricConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenVolumetric"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OpenVolumetricConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/OpenVolumetricConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenVolumetricConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenVolumetric"
)
