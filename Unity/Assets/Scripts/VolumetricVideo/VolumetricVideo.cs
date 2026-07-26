using System.IO;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Unity component that opens and plays a combined volumetric MP4.
///
/// Unity's DSP clock provides the playback timeline. Each update selects a
/// presentation frame; the native render callback publishes its matching
/// texture and geometry while the AudioClip pulls decoded PCM.
/// </summary>
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
    public double CurrentTime
    {
        get
        {
            if (m_playback_state == PlaybackState.PLAYING ||
                m_playback_state == PlaybackState.SCHEDULED)
            {
                double time = System.Math.Max(
                    0.0, AudioSettings.dspTime - m_start_time);
                if(enableLoop && Duration > 0.0)
                {
                    time %= Duration;
                }
                return time;
            }
            return m_playback_position;
        }
    }
    

    //----------------------------------------------------------
    // Start is called before the first frame update
    //----------------------------------------------------------
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

    //----------------------------------------------------------
    // Update is called once per frame
    //----------------------------------------------------------
    void Update()
    {
        // handle the case that playback has been scheduled
        if (m_playback_state == PlaybackState.SCHEDULED && AudioSettings.dspTime >= m_start_time)
        {
            m_playback_state = PlaybackState.PLAYING;
        }

        // If started then begin to update time
        if (m_playback_state == PlaybackState.PLAYING)
        {
            // Workout the frame number         
            double time = AudioSettings.dspTime - m_start_time;
            m_playback_position = time;

            // Set counter in decoder
            m_decoder.update(time);

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

    //----------------------------------------------------------
    // On Application Quit - stop decoding
    //----------------------------------------------------------
    void OnApplicationQuit() 
    {
        shutdown();
	}

    //----------------------------------------------------------
    // On Destroy - stop decoding
    //----------------------------------------------------------
    void OnDestroy() 
    {
        shutdown();
    }

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

    //----------------------------------------------------------
    // Option to schedult start based on a DSP time 
    // Needs to be in the future otherwise it will just start on load
    //----------------------------------------------------------
    public void set_scheduled_start(double dspTime)
    {
        m_playback_position = 0.0;
        m_start_time        = dspTime;
        m_audio_start_time  = dspTime;
        m_has_scheduled_start = true;
        m_playback_state    = PlaybackState.SCHEDULED;
        schedule_audio();
    }

    private void schedule_audio()
    {
        if(m_audio_source != null && m_audio_source.clip != null)
        {
            m_audio_source.time = (float)m_playback_position;
            m_audio_source.PlayScheduled(m_audio_start_time);
        }
    }

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
        m_playback_state = PlaybackState.SCHEDULED;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        schedule_audio();
    }

    public void PausePlayback()
    {
        if(m_playback_state != PlaybackState.PLAYING &&
            m_playback_state != PlaybackState.SCHEDULED)
        {
            return;
        }

        m_playback_position = System.Math.Min(CurrentTime, Duration);
        m_playback_state = PlaybackState.PAUSED;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
    }

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

    public void SeekRelative(double seconds)
    {
        Seek(CurrentTime + seconds);
    }

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


    //----------------------------------------------------------
    // On Destroy - stop decoding
    //----------------------------------------------------------
    private void OnValidate()
    {
        if(m_playback_state == PlaybackState.PLAYING)
        {
            m_decoder.set_colour_correction_values(luminaceCorrection, blueProjectionCorrection, redProjectionCorrection);
        }
    }

}
