using UnityEditor;
using UnityEngine;

namespace OpenVolumetric.Editor
{

/// <summary>
/// Keeps the common playback configuration compact while retaining adaptive
/// capability overrides and evaluation controls for advanced workflows.
/// </summary>
[CustomEditor(typeof(global::OpenVolumetric.OpenVolumetric))]
internal sealed class OpenVolumetricComponentEditor : UnityEditor.Editor
{
    private bool m_showAdaptiveAdvanced;
    private bool m_showEvaluation;

    public override void OnInspectorGUI()
    {
        serializedObject.Update();
        DrawPropertiesExcluding(
            serializedObject,
            "m_Script",
            "adaptiveMaximumTextureDimension",
            "adaptiveMaximumTextureBitrateMbps",
            "adaptiveMaximumGeometryBitrateMbps",
            "adaptiveMaximumBandwidthMbps",
            "enableLiveAdaptiveSwitching",
            "recordAdaptiveMetrics",
            "adaptiveMetricsFileName",
            "adaptiveMetricsInterval");

        if(serializedObject.FindProperty("useAdaptiveManifest").boolValue)
        {
            EditorGUILayout.Space();
            m_showAdaptiveAdvanced = EditorGUILayout.Foldout(
                m_showAdaptiveAdvanced,
                "Adaptive Advanced Settings",
                true);
            if(m_showAdaptiveAdvanced)
            {
                EditorGUI.indentLevel++;
                Draw("adaptiveMaximumTextureDimension");
                Draw("adaptiveMaximumTextureBitrateMbps");
                Draw("adaptiveMaximumGeometryBitrateMbps");
                Draw("adaptiveMaximumBandwidthMbps");
                Draw("enableLiveAdaptiveSwitching");
                EditorGUI.indentLevel--;
            }

            m_showEvaluation = EditorGUILayout.Foldout(
                m_showEvaluation,
                "Adaptive Evaluation",
                true);
            if(m_showEvaluation)
            {
                EditorGUI.indentLevel++;
                Draw("recordAdaptiveMetrics");
                if(serializedObject.FindProperty(
                    "recordAdaptiveMetrics").boolValue)
                {
                    Draw("adaptiveMetricsFileName");
                    Draw("adaptiveMetricsInterval");
                }
                EditorGUI.indentLevel--;
            }
        }
        serializedObject.ApplyModifiedProperties();
    }

    private void Draw(string propertyName)
    {
        EditorGUILayout.PropertyField(serializedObject.FindProperty(propertyName));
    }
}

}
