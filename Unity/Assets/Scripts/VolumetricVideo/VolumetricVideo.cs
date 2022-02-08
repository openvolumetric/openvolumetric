using System.IO;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;



public class VolumetricVideo : MonoBehaviour
{
    // -----------------------------
    // Public Member variables 
    public bool debug               = false;
    public string video_filename;
    public string mesh_filepattern;
    public int mesh_start_index;
    public int mesh_stop_index;



    // -----------------------------
    // Private 
    
    // Volumetric Video
    private VolumetricVideoDecoder m_decoder;

    // Mesh Filter - the form of the object
    private Mesh m_mesh;

    // Mesh Renderer - the way the object looks on screen
    private MeshRenderer m_mesh_renderer;

    // Start time
    private double m_start_time;

    private bool started = false;

    // Start is called before the first frame update
    void Start()
    {
        // create new volumetric video decoder
        m_decoder = new VolumetricVideoDecoder(debug);

        // Geometry part
        gameObject.AddComponent<MeshFilter>();
        m_mesh = gameObject.GetComponent<MeshFilter>().mesh;

        //
       if (!m_decoder.init_mesh(ref m_mesh))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init Mesh Buffers");
        }

         // Try to load mesh
        if(!m_decoder.init_mesh_data(mesh_filepattern, mesh_start_index, mesh_stop_index))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
        }

        // Add/Get Mesh renderer
        gameObject.AddComponent<MeshRenderer>();
        m_mesh_renderer = gameObject.GetComponent<MeshRenderer>();
        
        // Try to load data
        string filepath = Path.Combine(Application.streamingAssetsPath, video_filename);
        if(!m_decoder.init_texture(ref m_mesh_renderer, filepath))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
        }

  
        // Start Decoder
        if(!m_decoder.start_decoding())
        {
            Debug.LogError("VolumetricVideo::Start - Failed to start decoding");
        }

        // Start time of application         
        m_start_time = AudioSettings.dspTime;
    }

    //
    // Update is called once per frame
    //
    void Update()
    {
        if(!started)
        {
            started = true;
            // Start time of application         
            m_start_time = AudioSettings.dspTime;
        }

        // Workout the frame number         
        double time =  AudioSettings.dspTime - m_start_time;
        
        // Set counter in decoder
        m_decoder.update(time);
    }

    //
    //
    //
    void OnApplicationQuit() 
    {
		m_decoder.stop_decoding();
	}

    //
    //
    //
	void OnDestroy() 
    {
		m_decoder.stop_decoding();
	}



}
