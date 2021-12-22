using System.Collections;
using System.Collections.Generic;
using UnityEngine;

using System.Runtime.InteropServices;
using System;

public class VolumetricVideoDecoder 
{
    const string DLLNAME = "NativePlugin";
    [DllImport(DLLNAME)]
    private static extern int volumetricvideo_init(ref int ID);

    [DllImport(DLLNAME)]
    private static extern void volumetricvideo_quit(int ID);
    
    [DllImport(DLLNAME)]
    private static extern void volumetricvideo_open_external_console();

    [DllImport(DLLNAME)]
    private static extern void volumetricvideo_close_external_console();


    int m_instance_id = -1;
    bool debug;

    public VolumetricVideoDecoder( bool _debug )
    {
        Debug.Log("VolumetricVideoDecoder - Constructor");

        debug = _debug;

        if(debug)
        {
            Debug.Log("VolumetricVideoDecoder - Opening External Console");
            volumetricvideo_open_external_console();
        }
    }

    ~VolumetricVideoDecoder()
    {
        Debug.Log(String.Format("VolumetricVideoDecoder::~VolumetricVideoDecoder - Destructor - id: {0}", m_instance_id));
        volumetricvideo_quit(m_instance_id);

        Debug.Log(String.Format("VolumetricVideoDecoder::~VolumetricVideoDecoder - Closing External Console - id: {0}", m_instance_id));
        volumetricvideo_close_external_console();
    }


    public bool init(string video_file)
    {
        // Init Instance of Volumetric Video decoder
        if(volumetricvideo_init(ref m_instance_id) == -1)
        {
            Debug.Log("VolumetricVideoDecoder::init - failed to load");
            return false;
        }
        Debug.Log( String.Format("VolumetricVideoDecoder::init - instance ID: {0}",m_instance_id) );

        //
        return true;
    }


}
