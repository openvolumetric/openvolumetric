cmake_minimum_required(VERSION 3.20)

project(OpenVolumetricConsumer LANGUAGES CXX)

find_package(OpenVolumetric CONFIG REQUIRED)

add_executable(OpenVolumetricRuntimeConsumer runtime.cpp)
target_compile_features(OpenVolumetricRuntimeConsumer PRIVATE cxx_std_17)
target_link_libraries(OpenVolumetricRuntimeConsumer PRIVATE
  OpenVolumetric::Core)

if(TARGET OpenVolumetric::Authoring)
  add_executable(OpenVolumetricAuthoringConsumer authoring.cpp)
  target_compile_features(OpenVolumetricAuthoringConsumer PRIVATE cxx_std_17)
  target_link_libraries(OpenVolumetricAuthoringConsumer PRIVATE
    OpenVolumetric::Authoring)
endif()
