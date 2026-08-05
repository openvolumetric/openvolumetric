using UnrealBuildTool;

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
				"OpenVolumetricSDK",
				"Projects",
				"RenderCore",
				"RHI"
			});
	}
}
