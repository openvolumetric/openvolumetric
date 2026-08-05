find_package(doctest CONFIG REQUIRED)

add_executable(OpenVolumetricCoreTests
  CoreTests.cpp
)
openvolumetric_apply_build_options(OpenVolumetricCoreTests)

target_link_libraries(OpenVolumetricCoreTests
  PRIVATE
    OpenVolumetricAuthoringCore
    OpenVolumetricCore
    doctest::doctest
)

target_compile_definitions(OpenVolumetricCoreTests
  PRIVATE
    OPENVOLUMETRIC_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/openvolumetric-test.mp4"
    OPENVOLUMETRIC_INDEPENDENT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/openvolumetric-independent.mp4"
    OPENVOLUMETRIC_CORRUPT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/openvolumetric-corrupt-update.mp4"
    OPENVOLUMETRIC_NO_AUDIO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/openvolumetric-no-audio.mp4"
    OPENVOLUMETRIC_FRAGMENTED_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/openvolumetric-fragmented.mp4"
    OPENVOLUMETRIC_ADAPTIVE_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/adaptive/manifest.json"
)

add_test(
  NAME OpenVolumetricCoreTests
  COMMAND OpenVolumetricCoreTests
)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_executable(OpenVolumetricHttpTransportTests
    HttpTransportTests.cpp
  )
  openvolumetric_apply_build_options(OpenVolumetricHttpTransportTests)
  target_link_libraries(OpenVolumetricHttpTransportTests
    PRIVATE
      OpenVolumetricCore
  )
  add_test(
    NAME OpenVolumetricHttpTransportTests
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/http_transport_harness.py"
      "$<TARGET_FILE:OpenVolumetricHttpTransportTests>"
  )
  set_tests_properties(OpenVolumetricHttpTransportTests PROPERTIES
    TIMEOUT 30
  )
endif()

# This target is intentionally not part of the normal build. Maintainers use it
# through fixtures/regenerate_fixture.sh when the packet/container format
# changes; ordinary test runs consume the resulting small committed fixture.
add_executable(OpenVolumetricFixtureGenerator EXCLUDE_FROM_ALL
  FixtureGenerator.cpp
)
openvolumetric_apply_build_options(OpenVolumetricFixtureGenerator)

target_link_libraries(OpenVolumetricFixtureGenerator
  PRIVATE
    OpenVolumetricAuthoringCore
)
