using UnrealBuildTool;
using System.IO;

public class OpenVolRuntime : ModuleRules
{
	public OpenVolRuntime(ReadOnlyTargetRules Target) : base(Target)
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
				"Projects",
				"RenderCore",
				"RHI"
			});

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string RepositoryRoot = Path.GetFullPath(
				Path.Combine(ModuleDirectory, "..", "..", "..", "..", ".."));
			string NativeRoot = Path.Combine(RepositoryRoot, "OpenVolNative");
			string NativeBuild = Path.Combine(
				NativeRoot, "build", "unreal-host-macos14");
			string Installed = Path.Combine(
				NativeBuild, "vcpkg_installed", "arm64-osx-openvol");

			PrivateIncludePaths.AddRange(
				new[]
				{
					Path.Combine(NativeRoot, "src", "core", "container"),
					Path.Combine(NativeRoot, "src", "core", "decoding"),
					Path.Combine(NativeRoot, "src", "core", "geometry"),
					Path.Combine(NativeRoot, "src", "core", "media"),
					Path.Combine(NativeRoot, "src", "core", "support")
				});
			PublicSystemIncludePaths.Add(Path.Combine(Installed, "include"));

			PublicAdditionalLibraries.AddRange(
				new[]
				{
					Path.Combine(NativeBuild, "src", "core", "libOpenVolCore.a"),
					Path.Combine(Installed, "lib", "libavformat.a"),
					Path.Combine(Installed, "lib", "libavcodec.a"),
					Path.Combine(Installed, "lib", "libswresample.a"),
					Path.Combine(Installed, "lib", "libavutil.a"),
					Path.Combine(Installed, "lib", "libdraco.a")
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
