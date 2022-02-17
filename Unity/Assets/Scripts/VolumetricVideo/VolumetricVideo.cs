using System.IO;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;



public class VolumetricVideo : MonoBehaviour
{
    //----------------------------------------------------------
    // Public Member variables 
    //----------------------------------------------------------

    // Geometry inputs
    [Header("Geometry")]
    [Tooltip("Pattern to mesh file e.g. /path/to/mesh/%03d.bin.\n" +
             "Geometry should be encoded using google draco.")]
    public string meshFilepattern;
    [Tooltip("Index of first frame")]
    public int meshStartIndex;
    [Tooltip("Index of last frame")]
    public int meshStopIndex;

    // Texture Input
    [Header("Texture")]
    [Tooltip("Path to video file containing texture")]
    public string videoFilename;

    // Playback settings
    [Header("Playback")]
    [Tooltip("When enabled the content will loop")]
    public bool enableLoop = false;
    [Tooltip("Enables playback to be started via a script ")]
    public bool enableScriptedStart;

    // Debug options
    [Header("Debug")]
    [Tooltip("Enable debug, will launch an external console")]
    public bool debug = false;



    //----------------------------------------------------------
    // Private 
    //----------------------------------------------------------

    // Volumetric Video
    private VolumetricVideoDecoder m_decoder;

    // Mesh Filter - the form of the object
    private Mesh m_mesh;

    // Mesh Renderer - the way the object looks on screen
    private MeshRenderer m_mesh_renderer;

    // Start time
    private double m_start_time;

    // Enum for the Playback state
    enum PlaybackState
    {
        READY,
        SCHEDULED,
        STOPPED,
        PLAYING,
    };

    // Playback State
    private PlaybackState m_playback_state;



    //----------------------------------------------------------
    // Start is called before the first frame update
    //----------------------------------------------------------
    void Start()
    {
        // create new volumetric video decoder
        m_decoder = new VolumetricVideoDecoder(debug);

        // Geometry 
        gameObject.AddComponent<MeshFilter>();
        m_mesh = gameObject.GetComponent<MeshFilter>().mesh;

        //
       if (!m_decoder.init_mesh(ref m_mesh))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init Mesh Buffers");
        }

         // Try to load mesh
        if(!m_decoder.init_mesh_data(meshFilepattern, meshStartIndex, meshStopIndex))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
        }

        // Add/Get Mesh renderer
        gameObject.AddComponent<MeshRenderer>();
        m_mesh_renderer = gameObject.GetComponent<MeshRenderer>();
        
        // Try to load data
        string filepath = Path.Combine(Application.streamingAssetsPath, videoFilename);
        if(!m_decoder.init_texture(ref m_mesh_renderer, filepath))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
        }

        // Start Decoder
        if(!m_decoder.start_decoding())
        {
            Debug.LogError("VolumetricVideo::Start - Failed to start decoding");
        }

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

            if(!enableLoop)
            {
                if(m_decoder.ContentLooped)
                {
                    m_playback_state = PlaybackState.STOPPED;
                    m_mesh_renderer.enabled = false;
                }
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


}
