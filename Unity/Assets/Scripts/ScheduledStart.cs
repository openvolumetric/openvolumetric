using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ScheduledStart : MonoBehaviour
{
    public double startTimeDSP;


    // Start is called before the first frame update
    void Start()
    {
        //
        double startTime = AudioSettings.dspTime + startTimeDSP; //Start in 10 second
        Debug.Log(string.Format("DSP Start Time: {0}", startTime));

        var vv_objs = FindObjectsOfType<VolumetricVideo>();
        foreach (VolumetricVideo vv in vv_objs)
        {
            vv.set_scheduled_start(startTime);
        }




    }

    // Update is called once per frame
    void Update()
    {
        
    }
}
