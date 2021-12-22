using System.Collections;
using System.Collections.Generic;
using UnityEngine;



public class VolumetricVideo : MonoBehaviour
{

    public string video_filename;

    public bool debug = false;


    private VolumetricVideoDecoder decoder;


    // Start is called before the first frame update
    void Start()
    {
        decoder = new VolumetricVideoDecoder(debug);

        decoder.init(video_filename);
        
    }

    // Update is called once per frame
    void Update()
    {
        
    }
}
