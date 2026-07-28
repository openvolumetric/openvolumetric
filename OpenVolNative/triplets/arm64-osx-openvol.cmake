set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)

# Match Unreal Engine 5.8's default minimum macOS version. Keeping this in the
# triplet ensures dependency archives and OpenVolCore use the same target.
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
