using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.Profiling;

namespace OpenVolumetric
{

/// <summary>
/// Minimal headset diagnostics and controller-operated playback controls.
/// The overlay uses a camera-attached TextMesh and does not require a Canvas,
/// EventSystem, controller rays, or scene setup.
/// </summary>
public sealed class OpenVolumetricDeveloperOverlay : MonoBehaviour
{
    private const float SeekSeconds = 10.0F;

    private OpenVolumetric m_player;
    private TextMesh m_text;
    private InputAction m_togglePlayback;
    private InputAction m_toggleLoop;
    private InputAction m_seekBackward;
    private InputAction m_seekForward;
    private InputAction m_toggleOverlay;
    private bool m_visible = true;
    private float m_smoothedDelta;
    private float m_nextTextUpdate;

    /// <summary>Adds one overlay to player unless it already has one.</summary>
    public static void Attach(OpenVolumetric player)
    {
        if(player == null ||
            player.GetComponent<OpenVolumetricDeveloperOverlay>() != null)
        {
            return;
        }

        OpenVolumetricDeveloperOverlay overlay =
            player.gameObject.AddComponent<OpenVolumetricDeveloperOverlay>();
        overlay.m_player = player;
    }

    /// <summary>Captures the colocated player component.</summary>
    private void Awake()
    {
        m_player = m_player != null ? m_player : GetComponent<OpenVolumetric>();
        CreateActions();
    }

    /// <summary>Creates controller actions and the camera-attached display.</summary>
    private void Start()
    {
        CreateDisplay();
    }

    /// <summary>Enables controller input while the overlay is active.</summary>
    private void OnEnable()
    {
        SetActionsEnabled(true);
    }

    /// <summary>Disables controller input without destroying bindings.</summary>
    private void OnDisable()
    {
        SetActionsEnabled(false);
    }

    /// <summary>Disposes runtime-created InputActions.</summary>
    private void OnDestroy()
    {
        m_togglePlayback?.Dispose();
        m_toggleLoop?.Dispose();
        m_seekBackward?.Dispose();
        m_seekForward?.Dispose();
        m_toggleOverlay?.Dispose();
    }

    /// <summary>
    /// Handles button edges and periodically refreshes playback diagnostics.
    /// </summary>
    private void Update()
    {
        if(m_player == null)
        {
            return;
        }

        if(m_togglePlayback.WasPressedThisFrame())
        {
            m_player.TogglePlayPause();
        }
        if(m_toggleLoop.WasPressedThisFrame())
        {
            m_player.ToggleLoop();
        }
        if(m_seekBackward.WasPressedThisFrame())
        {
            m_player.SeekRelative(-SeekSeconds);
        }
        if(m_seekForward.WasPressedThisFrame())
        {
            m_player.SeekRelative(SeekSeconds);
        }
        if(m_toggleOverlay.WasPressedThisFrame())
        {
            m_visible = !m_visible;
            if(m_text != null)
            {
                m_text.gameObject.SetActive(m_visible);
            }
        }

        m_smoothedDelta = Mathf.Lerp(
            m_smoothedDelta <= 0.0F ? Time.unscaledDeltaTime : m_smoothedDelta,
            Time.unscaledDeltaTime,
            0.05F);
        if(m_visible && m_text != null && Time.unscaledTime >= m_nextTextUpdate)
        {
            UpdateText();
            m_nextTextUpdate = Time.unscaledTime + 0.25F;
        }
    }

    /// <summary>Creates lightweight actions using direct XR control paths.</summary>
    private void CreateActions()
    {
        m_togglePlayback = ButtonAction(
            "PlayPause", "<XRController>{RightHand}/primaryButton");
        m_toggleLoop = ButtonAction(
            "Loop", "<XRController>{RightHand}/secondaryButton");
        m_seekBackward = ButtonAction(
            "SeekBack", "<XRController>{LeftHand}/primaryButton");
        m_seekForward = ButtonAction(
            "SeekForward", "<XRController>{LeftHand}/secondaryButton");
        m_toggleOverlay = ButtonAction(
            "Overlay", "<XRController>{LeftHand}/menuButton");
    }

    /// <summary>Creates one button action with a single binding.</summary>
    private static InputAction ButtonAction(string name, string binding)
    {
        return new InputAction(
            name, InputActionType.Button, binding, interactions: "Press");
    }

    /// <summary>Enables or disables every overlay action as a group.</summary>
    private void SetActionsEnabled(bool enabled)
    {
        SetEnabled(m_togglePlayback, enabled);
        SetEnabled(m_toggleLoop, enabled);
        SetEnabled(m_seekBackward, enabled);
        SetEnabled(m_seekForward, enabled);
        SetEnabled(m_toggleOverlay, enabled);
    }

    /// <summary>Changes one optional action's enabled state.</summary>
    private static void SetEnabled(InputAction action, bool enabled)
    {
        if(action == null)
        {
            return;
        }
        if(enabled)
        {
            action.Enable();
        }
        else
        {
            action.Disable();
        }
    }

    /// <summary>
    /// Creates a world-space TextMesh parented to the main camera.
    /// </summary>
    private void CreateDisplay()
    {
        Camera camera = Camera.main;
        if(camera == null)
        {
            Debug.LogWarning(
                "OpenVolumetricDeveloperOverlay - no MainCamera found");
            enabled = false;
            return;
        }

        GameObject display = new GameObject("OpenVolumetric Diagnostics");
        display.transform.SetParent(camera.transform, false);
        display.transform.localPosition = new Vector3(-0.42F, 0.28F, 1.1F);
        display.transform.localRotation = Quaternion.identity;

        m_text = display.AddComponent<TextMesh>();
        m_text.anchor = TextAnchor.UpperLeft;
        m_text.alignment = TextAlignment.Left;
        m_text.fontSize = 48;
        m_text.characterSize = 0.012F;
        m_text.color = new Color(0.35F, 1.0F, 0.65F, 1.0F);
        m_text.richText = false;
        UpdateText();
    }

    /// <summary>Formats current playback and device performance information.</summary>
    private void UpdateText()
    {
        float fps = m_smoothedDelta > 0.0F ? 1.0F / m_smoothedDelta : 0.0F;
        float frameMs = m_smoothedDelta * 1000.0F;
        long memoryMb = Profiler.GetTotalAllocatedMemoryLong() /
            (1024L * 1024L);
        string error = m_player.LastError;
        m_text.text = string.Format(
            "OPENVOLUMETRIC\n" +
            "{0}  {1:F1}/{2:F1}s  Loop:{3}\n" +
            "{4:F1} fps  {5:F2} ms  Memory:{6} MB\n" +
            "{7} | {8}{9}\n" +
            "A Play/Pause  B Loop\n" +
            "X -10s  Y +10s  Menu Hide",
            m_player.State,
            m_player.CurrentTime,
            m_player.Duration,
            m_player.enableLoop ? "On" : "Off",
            fps,
            frameMs,
            memoryMb,
            SystemInfo.graphicsDeviceType,
            SystemInfo.deviceModel,
            string.IsNullOrEmpty(error) ? string.Empty : "\nERROR: " + error);
    }
}

}
