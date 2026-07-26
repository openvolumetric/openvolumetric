cmake_minimum_required(VERSION 3.20)

project(VolumetricVideoNativePlugin LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/core)

# Engine integrations link to the portable decoder core. More integrations,
# such as Unreal, can be added alongside Unity without changing the core.
if(WIN32 OR APPLE)
  add_subdirectory(integrations/unity)
else()
  message(STATUS
    "Building VolumetricVideoCore only; no engine integration is available for this platform")
endif()
