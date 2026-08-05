using UnrealBuildTool;
using System;
using System.IO;

public class OpenVolumetricSDK : ModuleRules
{
	public OpenVolumetricSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Mac &&
			Target.Platform != UnrealTargetPlatform.Win64)
		{
			throw new BuildException(
				"OpenVolumetric currently supplies Unreal SDK archives only for Mac and Win64.");
		}

		string SdkRoot = Environment.GetEnvironmentVariable(
			"OPENVOLUMETRIC_UNREAL_SDK");
		if (String.IsNullOrEmpty(SdkRoot))
		{
			SdkRoot = Path.Combine(
				ModuleDirectory, "..", "ThirdParty", "OpenVolumetricSDK");
		}
		SdkRoot = Path.GetFullPath(SdkRoot);
		string IncludeRoot = Path.Combine(SdkRoot, "include", "OpenVolumetric");
		string PlatformName = Target.Platform == UnrealTargetPlatform.Mac
			? "Mac"
			: "Win64";
		string LibraryRoot = Path.Combine(SdkRoot, "lib", PlatformName);
		if (!Directory.Exists(IncludeRoot) || !Directory.Exists(LibraryRoot))
		{
			throw new BuildException(
				"OpenVolumetric SDK is not staged at '{0}'. Run tools/stage_unreal_sdk.py " +
				"or set OPENVOLUMETRIC_UNREAL_SDK.", SdkRoot);
		}

		PublicSystemIncludePaths.Add(IncludeRoot);
		string[] Libraries = Target.Platform == UnrealTargetPlatform.Mac
			? new[]
			{
				"libOpenVolumetricAuthoringCore.a", "libOpenVolumetricCore.a",
				"libavformat.a", "libavcodec.a", "libswresample.a",
				"libavutil.a", "libdraco.a", "libcurl.a", "libssl.a",
				"libcrypto.a", "libz.a"
			}
			: new[]
			{
				"OpenVolumetricAuthoringCore.lib", "OpenVolumetricCore.lib",
				"avformat.lib", "avcodec.lib", "swresample.lib", "avutil.lib",
				"draco.lib", "libcurl.lib", "libssl.lib", "libcrypto.lib",
				"zlib.lib"
			};
		foreach (string Library in Libraries)
		{
			string Archive = Path.Combine(LibraryRoot, Library);
			if (!File.Exists(Archive))
			{
				throw new BuildException(
					"OpenVolumetric SDK archive is missing: {0}", Archive);
			}
			PublicAdditionalLibraries.Add(Archive);
			ExternalDependencies.Add(Archive);
		}

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(
				new[]
				{
					"AudioToolbox", "CoreFoundation", "CoreMedia",
					"CoreServices", "CoreVideo", "VideoToolbox"
				});
		}
		else
		{
			PublicSystemLibraries.AddRange(
				new[]
				{
					"avrt.lib", "bcrypt.lib", "crypt32.lib", "iphlpapi.lib",
					"mf.lib", "mfplat.lib", "mfuuid.lib", "normaliz.lib",
					"ole32.lib", "secur32.lib", "strmiids.lib", "user32.lib",
					"ws2_32.lib"
				});
		}
	}
}
