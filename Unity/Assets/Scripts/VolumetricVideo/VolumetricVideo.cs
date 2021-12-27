using System.Collections;
using System.Collections.Generic;
using UnityEngine;



public class VolumetricVideo : MonoBehaviour
{
    // -----------------------------
    // Public Member variables 
    public bool debug               = false;
    public string video_filename    = "";


    // -----------------------------
    // Private 
    
    // Volumetric Video
    private VolumetricVideoDecoder m_decoder;

    // Mesh Renderer
    private MeshRenderer m_mesh_renderer;

    // Start time
    double m_start_time;

    // Start is called before the first frame update
    void Start()
    {
        // Add/Get Mesh renderer
        gameObject.AddComponent<MeshRenderer>();
        m_mesh_renderer = gameObject.GetComponent<MeshRenderer>();
        
        // create new volumetric video decoder
        m_decoder = new VolumetricVideoDecoder(debug);
        
        // Try to load data
        if(!m_decoder.init(ref m_mesh_renderer, video_filename))
        {
            Debug.LogError("VolumetricVideo::Start - Failed to init VolumetricVideoDecoder");
        }

        // TODO Geometry


        // Start Decoder
        if(!m_decoder.start_decoding())
        {
           // Debug.LogError("VolumetricVideo::Start - Failed to start decoding");
        }

        // Start time of application         
        m_start_time = AudioSettings.dspTime;
    }

    // Update is called once per frame
    void Update()
    {
        // Workout the frame number         
        double time =  AudioSettings.dspTime - m_start_time;
        
        // Set counter in decoder
        m_decoder.update(time);
    }





}
