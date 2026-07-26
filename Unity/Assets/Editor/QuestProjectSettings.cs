using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.XR.OpenXR;

/// <summary>
/// Keeps the checked-in Android settings compatible with the Quest build.
///
/// Unity 6 build profiles can override serialized PlayerSettings values. Using
/// the public settings APIs here ensures the active configuration selects the
/// same requirements as the repository baseline.
/// </summary>
[InitializeOnLoad]
internal static class QuestProjectSettings
{
    static QuestProjectSettings()
    {
        EditorApplication.delayCall += Apply;
    }

    private static void Apply()
    {
        PlayerSettings.Android.applicationEntry =
            AndroidApplicationEntry.GameActivity;
        PlayerSettings.Android.targetArchitectures =
            AndroidArchitecture.ARM64;
        PlayerSettings.SetScriptingBackend(
            UnityEditor.Build.NamedBuildTarget.Android,
            ScriptingImplementation.IL2CPP);
        PlayerSettings.SetGraphicsAPIs(
            BuildTarget.Android,
            new[] { GraphicsDeviceType.Vulkan });
        PlayerSettings.defaultInterfaceOrientation =
            UIOrientation.LandscapeLeft;

        OpenXRSettings settings =
            OpenXRSettings.GetSettingsForBuildTargetGroup(
                BuildTargetGroup.Android);
        if(settings != null)
        {
            settings.latencyOptimization =
                OpenXRSettings.LatencyOptimization.PrioritizeInputPolling;
            EditorUtility.SetDirty(settings);
        }

        AssetDatabase.SaveAssets();
        Debug.Log(
            "QuestProjectSettings - Android architecture: " +
            PlayerSettings.Android.targetArchitectures +
            ", entry point: " +
            PlayerSettings.Android.applicationEntry);
    }
}
