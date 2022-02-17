using System.IO;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEditor;


public class VolumetricVideo : MonoBehaviour
{
    //----------------------------------------------------------
    // Public Member variables 
    //----------------------------------------------------------

    // Geometry Settings
    [Header("Geometry Settings")]
    [Tooltip("Pattern to mesh file e.g. /path/to/mesh/%03d.bin.\n" +
             "Geometry should be encoded using google draco.")]
    public string meshFilepattern;
    [Tooltip("Index of first frame")]
    public int meshStartIndex;
    [Tooltip("Index of last frame")]
    public int meshStopIndex;

    // Texture Settings
    [Header("Texture Settings")]
    [Tooltip("Path to video file containing texture")]
    public string videoFilename;
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

    // Start time
    private double m_start_time;

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
    void Start()
    {
        // Add components required to draw mesh
        MeshFilter mesh_filter = gameObject.AddComponent<MeshFilter>();
        MeshRenderer mesh_renderer = gameObject.AddComponent<MeshRenderer>();

        // create new volumetric video decoder
        m_decoder = new VolumetricVideoDecoder(debug);
             
        // Init/Load mesh
        if(!m_decoder.init_mesh_data(meshFilepattern, meshStartIndex, meshStopIndex))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");        
        }

        // Assigned Mesh to mesh filter Object
        mesh_filter.mesh = m_decoder.Mesh;
        
        // Load Texture data
        string filepath = Path.Combine(Application.streamingAssetsPath, videoFilename);
        if(!m_decoder.init_texture(ref mesh_renderer, filepath))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
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

        // Set Player State to Initialised
        m_playback_state = PlaybackState.PLAYING;

        // If not scheduled start then 
        if(!enableScriptedStart)
        {
            m_start_time = AudioSettings.dspTime ;
            m_playback_state = PlaybackState.SCHEDULED;
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
            // Start time of application         
            m_start_time = AudioSettings.dspTime;
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
		if(m_decoder.stop_decoding())
        {
            m_playback_state = PlaybackState.STOPPED;
        }
	}

    //----------------------------------------------------------
    // On Destroy - stop decoding
    //----------------------------------------------------------
    void OnDestroy() 
    {
        if (m_decoder.stop_decoding())
        {
            m_playback_state = PlaybackState.STOPPED;
        }
    }

    //----------------------------------------------------------
    // Option to schedult start based on a DSP time 
    // Needs to be in the future otherwise it will just start on load
    //----------------------------------------------------------
    public void set_scheduled_start(double dspTime)
    {
        m_start_time        = dspTime;
        m_playback_state    = PlaybackState.SCHEDULED;
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
