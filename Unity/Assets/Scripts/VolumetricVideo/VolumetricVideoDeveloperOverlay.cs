using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.Profiling;

/// <summary>
/// Minimal headset diagnostics and controller-operated playback controls.
/// The overlay uses a camera-attached TextMesh and does not require a Canvas,
/// EventSystem, controller rays, or scene setup.
/// </summary>
public sealed class VolumetricVideoDeveloperOverlay : MonoBehaviour
{
    private const float SeekSeconds = 10.0F;

    private VolumetricVideo m_player;
    private TextMesh m_text;
    private InputAction m_togglePlayback;
    private InputAction m_toggleLoop;
    private InputAction m_seekBackward;
    private InputAction m_seekForward;
    private InputAction m_toggleOverlay;
    private bool m_visible = true;
    private float m_smoothedDelta;
    private float m_nextTextUpdate;

    public static void Attach(VolumetricVideo player)
    {
        if(player == null ||
            player.GetComponent<VolumetricVideoDeveloperOverlay>() != null)
        {
            return;
        }

        VolumetricVideoDeveloperOverlay overlay =
            player.gameObject.AddComponent<VolumetricVideoDeveloperOverlay>();
        overlay.m_player = player;
    }

    private void Awake()
    {
        m_player = m_player != null ? m_player : GetComponent<VolumetricVideo>();
        CreateActions();
    }

    private void Start()
    {
        CreateDisplay();
    }

    private void OnEnable()
    {
        SetActionsEnabled(true);
    }

    private void OnDisable()
    {
        SetActionsEnabled(false);
    }

    private void OnDestroy()
    {
        m_togglePlayback?.Dispose();
        m_toggleLoop?.Dispose();
        m_seekBackward?.Dispose();
        m_seekForward?.Dispose();
        m_toggleOverlay?.Dispose();
    }

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

    private static InputAction ButtonAction(string name, string binding)
    {
        return new InputAction(
            name, InputActionType.Button, binding, interactions: "Press");
    }

    private void SetActionsEnabled(bool enabled)
    {
        SetEnabled(m_togglePlayback, enabled);
        SetEnabled(m_toggleLoop, enabled);
        SetEnabled(m_seekBackward, enabled);
        SetEnabled(m_seekForward, enabled);
        SetEnabled(m_toggleOverlay, enabled);
    }

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

    private void CreateDisplay()
    {
        Camera camera = Camera.main;
        if(camera == null)
        {
            Debug.LogWarning(
                "VolumetricVideoDeveloperOverlay - no MainCamera found");
            enabled = false;
            return;
        }

        GameObject display = new GameObject("Volumetric Video Diagnostics");
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

    private void UpdateText()
    {
        float fps = m_smoothedDelta > 0.0F ? 1.0F / m_smoothedDelta : 0.0F;
        float frameMs = m_smoothedDelta * 1000.0F;
        long memoryMb = Profiler.GetTotalAllocatedMemoryLong() /
            (1024L * 1024L);
        string error = m_player.LastError;
        m_text.text = string.Format(
            "VOLUMETRIC VIDEO\n" +
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
