using UnrealBuildTool;
using System.IO;

public class OpenVolumetricRuntime : ModuleRules
{
	public OpenVolumetricRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AudioMixer",
				"GeometryCore",
				"GeometryFramework",
				"InputCore",
				"Projects",
				"RenderCore",
				"RHI"
			});

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string RepositoryRoot = Path.GetFullPath(
				Path.Combine(ModuleDirectory, "..", "..", "..", "..", ".."));
			string NativeRoot = Path.Combine(RepositoryRoot, "OpenVolumetricNative");
			string NativeBuild = Path.Combine(
				NativeRoot, "build", "unreal-host-macos14");
			string Installed = Path.Combine(
				NativeBuild, "vcpkg_installed", "arm64-osx-openvolumetric");

			PrivateIncludePaths.AddRange(
				new[]
				{
					Path.Combine(NativeRoot, "src", "core", "adaptive"),
					Path.Combine(NativeRoot, "src", "core", "container"),
					Path.Combine(NativeRoot, "src", "core", "decoding"),
					Path.Combine(NativeRoot, "src", "core", "geometry"),
					Path.Combine(NativeRoot, "src", "core", "io"),
					Path.Combine(NativeRoot, "src", "core", "media"),
					Path.Combine(NativeRoot, "src", "core", "support")
				});
			PublicSystemIncludePaths.Add(Path.Combine(Installed, "include"));

			PublicAdditionalLibraries.AddRange(
				new[]
				{
					// The core archive owns FFmpeg, Draco, and HTTP transport;
					// rebuild it before Unreal so this module links the current
					// runtime, then list every dependency required by that boundary.
					Path.Combine(NativeBuild, "src", "core", "libOpenVolumetricCore.a"),
					Path.Combine(Installed, "lib", "libavformat.a"),
					Path.Combine(Installed, "lib", "libavcodec.a"),
					Path.Combine(Installed, "lib", "libswresample.a"),
					Path.Combine(Installed, "lib", "libavutil.a"),
					Path.Combine(Installed, "lib", "libdraco.a"),
					Path.Combine(Installed, "lib", "libcurl.a"),
					Path.Combine(Installed, "lib", "libssl.a"),
					Path.Combine(Installed, "lib", "libcrypto.a"),
					Path.Combine(Installed, "lib", "libz.a")
				});

			PublicFrameworks.AddRange(
				new[]
				{
					"AudioToolbox",
					"CoreFoundation",
					"CoreMedia",
					"CoreServices",
					"CoreVideo",
					"VideoToolbox"
				});
		}
	}
}
