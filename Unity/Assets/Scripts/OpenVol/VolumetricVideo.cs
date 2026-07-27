using System.IO;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting.APIUpdating;

namespace OpenVol
{

/// <summary>
/// Unity component that opens and plays a combined volumetric MP4.
///
/// Unity's DSP clock provides the playback timeline. Each update selects a
/// presentation frame; the native render callback publishes its matching
/// texture and geometry while the AudioClip pulls decoded PCM.
/// </summary>
[MovedFrom(true, sourceNamespace: "", sourceAssembly: "Assembly-CSharp",
    sourceClassName: "VolumetricVideo")]
public class VolumetricVideo : MonoBehaviour
{
    //----------------------------------------------------------
    // Public Member variables 
    //----------------------------------------------------------

    // Volumetric Video Input
    [Header("Volumetric Video Input")]
    [Tooltip("Volumetric video file containing geometry, texture, and audio")]
    public string videoFilename;

    // Texture Settings
    [Header("Texture Settings")]
    [Tooltip("Luminance Correction - Y")]
    [Range(-0.2F, 0.2F)]
    public float luminaceCorrection=0.0F;
    [Tooltip("Chrominance Correction - Blue Projection - U")]
    [Range(-0.2F, 0.2F)]
    public float blueProjectionCorrection=0.0F;
    [Tooltip("Chrominance Correction - Red Projection - V")]
    [Range(-0.2F, 0.2F)]
    public float redProjectionCorrection=0.0F;

    // Playback settings
    [Header("Playback Settings")]
    [Tooltip("When enabled the content will loop")]
    public bool enableLoop = false;
    [Tooltip("Enables playback to be started via a script ")]
    public bool enableScriptedStart;
    [Tooltip("Show the controller-operated developer overlay in headset builds")]
    public bool enableDeveloperOverlay = true;

    // Debug options
    [Header("Debug Settings")]
    [Tooltip("Enable debug, will launch an external console")]
    public bool debug = false;

    //----------------------------------------------------------
    // Private 
    //----------------------------------------------------------

    // Volumetric Video
    private VolumetricVideoDecoder m_decoder;
    private AudioSource m_audio_source;

    // Start time
    private double m_start_time;
    private double m_audio_start_time;
    private bool m_has_scheduled_start;
    private double m_last_dsp_time;
    private bool m_has_last_dsp_time;
    private double m_decoder_lag_started = -1.0;
    private double m_last_decoder_recovery = -10.0;
    private bool m_decoder_recovering;
    private double m_decoder_recovery_target;

    // Enum for the Playback state
    public enum PlaybackState
    {
        INIT_FAIL = -1,
        UNINITIALISED,
        INITIALISED,
        SCHEDULED,
        PAUSED,
        STOPPED,
        PLAYING,
    };

    // Playback State
    private PlaybackState m_playback_state = PlaybackState.UNINITIALISED;
    private double m_playback_position;

    public PlaybackState State { get { return m_playback_state; } }
    public bool IsPlaying { get { return m_playback_state == PlaybackState.PLAYING; } }
    public double Duration { get { return m_decoder != null ? m_decoder.Duration : 0.0; } }
    public string LastError
    {
        get { return m_decoder != null ? m_decoder.LastError : string.Empty; }
    }
    public double CurrentTime
    {
        get
        {
            if (m_playback_state == PlaybackState.PLAYING ||
                m_playback_state == PlaybackState.SCHEDULED)
            {
                double time = System.Math.Max(0.0, m_playback_position);
                if(enableLoop && Duration > 0.0)
                {
                    time %= Duration;
                }
                return time;
            }
            return m_playback_position;
        }
    }
    

    /// <summary>
    /// Resolves the combined MP4, creates Unity render/audio resources, and
    /// starts the native decode workers.
    /// </summary>
    IEnumerator Start()
    {
        // Add components required to draw mesh
        MeshFilter mesh_filter = gameObject.AddComponent<MeshFilter>();
        MeshRenderer mesh_renderer = gameObject.AddComponent<MeshRenderer>();

        // create new volumetric video decoder
        m_decoder = new VolumetricVideoDecoder(debug);
             
        // The MP4 contains texture, geometry, and optional audio.
        string filepath = null;
        yield return StreamingAssetFile.PrepareReadablePath(
            videoFilename,
            path => filepath = path);
        if(string.IsNullOrEmpty(filepath))
        {
            Debug.LogError(
                "VolumetricVideo::Start - Failed to prepare volumetric video input");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }
        if(!m_decoder.init_texture(ref mesh_renderer, filepath))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }

        if(!m_decoder.init_mesh())
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init geometry");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }

        // Assign the native-backed mesh to the mesh filter.
        mesh_filter.mesh = m_decoder.Mesh;

        if(!m_decoder.init_audio())
        {
            Debug.LogError("VolumetricVideo::Start - Failed to initialise audio");
        }
        else if(m_decoder.HasAudio)
        {
            m_audio_source = gameObject.GetComponent<AudioSource>();
            if(m_audio_source == null)
            {
                m_audio_source = gameObject.AddComponent<AudioSource>();
            }
            m_audio_source.playOnAwake = false;
            m_audio_source.loop = enableLoop;
            m_audio_source.spatialBlend = 0.0F;
            m_audio_source.clip = m_decoder.AudioClip;
        }

#if UNITY_ANDROID && !UNITY_EDITOR
        if(enableDeveloperOverlay)
        {
            VolumetricVideoDeveloperOverlay.Attach(this);
        }
#endif

        //
        m_decoder.set_colour_correction_values(luminaceCorrection, blueProjectionCorrection, redProjectionCorrection);

        // Set Player State to Initialised
        m_playback_state = PlaybackState.INITIALISED;

        // Start Decoder
        if(!m_decoder.start_decoding())
        {
            Debug.LogError("VolumetricVideo::Start - Failed to start decoding");
        }

        m_playback_state = PlaybackState.INITIALISED;

        if(!enableScriptedStart)
        {
            set_scheduled_start(AudioSettings.dspTime + 0.1);
        }
        else if(m_has_scheduled_start)
        {
            m_playback_state = PlaybackState.SCHEDULED;
            schedule_audio();
        }
    }

    /// <summary>
    /// Advances the shared DSP-clock timeline and submits its presentation
    /// target to the native render callback once per Unity frame.
    /// </summary>
    void Update()
    {
        if(m_decoder_recovering)
        {
            m_decoder.update(m_decoder_recovery_target);
            double presented = m_decoder.LastPresentedTime;
            double tolerance = m_decoder.FrameRate > 0.0
                ? 1.0 / m_decoder.FrameRate
                : 0.034;
            if(presented >= m_decoder_recovery_target - tolerance)
            {
                m_decoder_recovering = false;
                m_playback_position = m_decoder_recovery_target;
                m_audio_start_time = AudioSettings.dspTime + 0.05;
                m_start_time =
                    m_audio_start_time - m_playback_position;
                m_last_dsp_time = m_audio_start_time;
                m_has_last_dsp_time = true;
                m_playback_state = PlaybackState.SCHEDULED;
                schedule_audio();
                Debug.Log("VolumetricVideo - synchronized recovery complete");
            }
            return;
        }

        // handle the case that playback has been scheduled
        if (m_playback_state == PlaybackState.SCHEDULED && AudioSettings.dspTime >= m_start_time)
        {
            m_playback_state = PlaybackState.PLAYING;
            m_last_dsp_time = m_start_time;
            m_has_last_dsp_time = true;
        }

        // If started then begin to update time
        if (m_playback_state == PlaybackState.PLAYING)
        {
            // Accumulate the DSP delta instead of deriving an absolute time.
            // Some Android audio-stack transitions briefly move dspTime
            // backwards. Treating that discontinuity as a loop used to seek
            // native playback to zero while the engine clock continued.
            double dspTime = AudioSettings.dspTime;
            if(!m_has_last_dsp_time)
            {
                m_last_dsp_time = dspTime;
                m_has_last_dsp_time = true;
            }
            double delta = dspTime - m_last_dsp_time;
            m_last_dsp_time = dspTime;
            if(delta >= 0.0 && delta <= 0.5)
            {
                m_playback_position += delta;
            }

            // Set counter in decoder
            m_decoder.update(m_playback_position);
            if(TryRecoverDecoderLag(dspTime))
            {
                return;
            }

            //
            if(!enableLoop && m_decoder.ContentLooped)
            {
                m_playback_state = PlaybackState.STOPPED;
                m_playback_position = m_decoder.Duration;
                m_decoder.MeshRenderer.enabled = false;
                if(m_audio_source != null)
                {
                    m_audio_source.Stop();
                }
            }
         }

    }

    /// <summary>
    /// Pauses all streams when native presentation remains behind the engine
    /// clock, allowing texture and geometry to catch up without losing sync.
    /// </summary>
    /// <returns>True when synchronized recovery was started.</returns>
    private bool TryRecoverDecoderLag(double dspTime)
    {
        double presented = m_decoder.LastPresentedTime;
        if(presented < 0.0)
        {
            return false;
        }

        double target = Duration > 0.0
            ? m_playback_position % Duration
            : m_playback_position;
        double lag = target - presented;
        if(lag < 0.0)
        {
            lag += Duration;
        }
        if(lag <= 0.5)
        {
            m_decoder_lag_started = -1.0;
            return false;
        }
        if(m_decoder_lag_started < 0.0)
        {
            m_decoder_lag_started = dspTime;
            return false;
        }
        if(dspTime - m_decoder_lag_started < 0.5 ||
            dspTime - m_last_decoder_recovery < 3.0)
        {
            return false;
        }

        m_last_decoder_recovery = dspTime;
        m_decoder_lag_started = -1.0;
        Debug.LogWarning(string.Format(
            "VolumetricVideo - decoder lagged by {0:F3}s; " +
            "pausing all streams to catch up at {1:F3}s",
            lag,
            target));
        m_decoder_recovering = true;
        m_decoder_recovery_target = target;
        m_playback_position = target;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        return true;
    }

    /// <summary>Stops native workers before Unity tears down the application.</summary>
    void OnApplicationQuit() 
    {
        shutdown();
	}

    /// <summary>Releases native and Unity resources when the component dies.</summary>
    void OnDestroy() 
    {
        shutdown();
    }

    /// <summary>
    /// Idempotently stops audio/decoding and disposes the native instance.
    /// </summary>
    private void shutdown()
    {
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        if(m_decoder != null)
        {
            if(m_decoder.DecoderStatus ==
                VolumetricVideoDecoder.DecoderState.STARTED)
            {
                m_decoder.stop_decoding();
            }
            m_decoder.Dispose();
            m_decoder = null;
        }
        m_playback_state = PlaybackState.STOPPED;
    }

    /// <summary>
    /// Schedules synchronized playback at an absolute future DSP timestamp.
    /// </summary>
    public void set_scheduled_start(double dspTime)
    {
        m_playback_position = 0.0;
        m_start_time        = dspTime;
        m_audio_start_time  = dspTime;
        m_has_scheduled_start = true;
        m_last_dsp_time = dspTime;
        m_has_last_dsp_time = true;
        m_playback_state    = PlaybackState.SCHEDULED;
        schedule_audio();
    }

    /// <summary>
    /// Schedules the streaming AudioClip from the current playback position.
    /// Texture and geometry use the same DSP-derived timeline.
    /// </summary>
    private void schedule_audio()
    {
        if(m_audio_source != null && m_audio_source.clip != null)
        {
            m_audio_source.time = (float)m_playback_position;
            m_audio_source.PlayScheduled(m_audio_start_time);
        }
    }

    /// <summary>Toggles between paused and scheduled/playing states.</summary>
    public void TogglePlayPause()
    {
        if(m_playback_state == PlaybackState.PLAYING ||
            m_playback_state == PlaybackState.SCHEDULED)
        {
            PausePlayback();
        }
        else
        {
            Play();
        }
    }

    /// <summary>
    /// Resumes from the current position using a short scheduling lead time.
    /// </summary>
    public void Play()
    {
        if(m_decoder == null ||
            m_playback_state == PlaybackState.INIT_FAIL ||
            m_playback_state == PlaybackState.UNINITIALISED)
        {
            return;
        }

        if(m_playback_position >= Duration)
        {
            Seek(0.0);
        }

        m_decoder.MeshRenderer.enabled = true;
        m_audio_start_time = AudioSettings.dspTime + 0.05;
        m_start_time = m_audio_start_time - m_playback_position;
        m_last_dsp_time = m_audio_start_time;
        m_has_last_dsp_time = true;
        m_playback_state = PlaybackState.SCHEDULED;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        schedule_audio();
    }

    /// <summary>Freezes the shared timeline and stops audio consumption.</summary>
    public void PausePlayback()
    {
        if(m_playback_state != PlaybackState.PLAYING &&
            m_playback_state != PlaybackState.SCHEDULED)
        {
            return;
        }

        m_playback_position = System.Math.Min(CurrentTime, Duration);
        m_has_last_dsp_time = false;
        m_playback_state = PlaybackState.PAUSED;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
    }

    /// <summary>
    /// Seeks every native stream and updates Unity audio to the clamped target.
    /// </summary>
    /// <returns>False when the decoder is unavailable or native seek fails.</returns>
    public bool Seek(double time)
    {
        if(m_decoder == null || Duration <= 0.0)
        {
            return false;
        }

        double target = System.Math.Max(0.0, System.Math.Min(time, Duration));
        bool resume = m_playback_state == PlaybackState.PLAYING ||
            m_playback_state == PlaybackState.SCHEDULED;
        if(!m_decoder.seek(target))
        {
            return false;
        }

        m_playback_position = target;
        m_decoder.MeshRenderer.enabled = true;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
            m_audio_source.time = (float)target;
        }

        if(resume)
        {
            Play();
        }
        else
        {
            m_playback_state = PlaybackState.PAUSED;
            m_decoder.update(target);
        }
        return true;
    }

    /// <summary>Seeks by a signed number of seconds from the current time.</summary>
    public void SeekRelative(double seconds)
    {
        Seek(CurrentTime + seconds);
    }

    /// <summary>Changes loop policy for both native playback and Unity audio.</summary>
    public void ToggleLoop()
    {
        enableLoop = !enableLoop;
        if(m_audio_source != null)
        {
            m_audio_source.loop = enableLoop;
        }
        if(enableLoop && m_decoder != null)
        {
            m_decoder.reset_loop_flag();
        }
    }


    /// <summary>
    /// Applies Inspector colour-correction edits during active playback.
    /// </summary>
    private void OnValidate()
    {
        if(m_playback_state == PlaybackState.PLAYING)
        {
            m_decoder.set_colour_correction_values(luminaceCorrection, blueProjectionCorrection, redProjectionCorrection);
        }
    }

}

}
