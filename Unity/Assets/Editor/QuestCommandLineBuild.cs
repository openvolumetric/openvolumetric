using System;
using System.Linq;

using UnityEditor;
using UnityEditor.Build.Reporting;

/// <summary>Provides a reproducible command-line build for the Quest APK.</summary>
internal static class QuestCommandLineBuild
{
    /// <summary>
    /// Builds all enabled EditorBuildSettings scenes for Android.
    ///
    /// Pass `-openVolumetricOutput path/to/file.apk` to override the default
    /// project-relative `test.apk` output used by local headset testing.
    /// </summary>
    public static void Build()
    {
        string[] scenes = EditorBuildSettings.scenes
            .Where(scene => scene.enabled)
            .Select(scene => scene.path)
            .ToArray();
        if(scenes.Length == 0)
        {
            throw new InvalidOperationException(
                "No enabled scenes are configured in Editor Build Settings.");
        }

        BuildPlayerOptions options = new BuildPlayerOptions
        {
            scenes = scenes,
            locationPathName = ReadOutputPath(),
            target = BuildTarget.Android,
            options = BuildOptions.None,
        };
        BuildReport report = BuildPipeline.BuildPlayer(options);
        if(report.summary.result != BuildResult.Succeeded)
        {
            throw new InvalidOperationException(
                "Quest build failed: " + report.summary.result);
        }

        UnityEngine.Debug.Log(
            "OpenVolumetric Quest APK built: " +
            report.summary.outputPath);
    }

    /// <summary>Reads the optional output argument from Unity's process command line.</summary>
    private static string ReadOutputPath()
    {
        string[] arguments = Environment.GetCommandLineArgs();
        for(int index = 0; index + 1 < arguments.Length; ++index)
        {
            if(arguments[index] == "-openVolumetricOutput")
            {
                return arguments[index + 1];
            }
        }
        return "test.apk";
    }
}
