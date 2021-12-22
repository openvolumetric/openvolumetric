#include "FFMPEGTools.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}




namespace ffmpegtools
{


	//------------------------------
	// Function to register ffmpeg components
	bool register_ffmpeg()
	{
		//deprecated in ffmpeg >= 4.0
		//av_register_all();

		return true;
	}

}

