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
    private bool m_has_scheduled_start;

    // Enum for the Playback state
    enum PlaybackState
    {
        INIT_FAIL = -1,
        UNINITIALISED,
        INITIALISED,
        SCHEDULED,
        STOPPED,
        PLAYING,
    };

    // Playback State
    private PlaybackState m_playback_state = PlaybackState.UNINITIALISED;
    

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

            // Set counter in decoder
            m_decoder.update(time);

            //
            if(!enableLoop && m_decoder.ContentLooped)
            {
                m_playback_state = PlaybackState.STOPPED;
                m_decoder.MeshRenderer.enabled = false;
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
        m_start_time        = dspTime;
        m_has_scheduled_start = true;
        m_playback_state    = PlaybackState.SCHEDULED;
        schedule_audio();
    }

    private void schedule_audio()
    {
        if(m_audio_source != null && m_audio_source.clip != null)
        {
            m_audio_source.PlayScheduled(m_start_time);
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
