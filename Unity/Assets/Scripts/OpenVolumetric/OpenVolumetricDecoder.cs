using System.Collections;
using System.Collections.Generic;

using UnityEngine;
using UnityEngine.Rendering;
using Unity.Collections;

using System.Runtime.InteropServices;
using System;

namespace OpenVolumetric
{

/// <summary>
/// Managed bridge to one native volumetric-video decoder instance.
///
/// Unity owns the Mesh, Material, Texture2D objects, and AudioClip. The native
/// plugin decodes the combined MP4 and writes into registered graphics buffers
/// during GL.IssuePluginEvent. Audio is pulled through the streaming AudioClip
/// callback.
/// </summary>
public class OpenVolumetricDecoder : IDisposable
{
    private const string PluginName = "OpenVolumetricUnityPlugin";

    // Entry-point names intentionally mirror the stable native C ABI.
    [DllImport(PluginName, EntryPoint = "GetRenderEventFunc")]
    private static extern IntPtr GetRenderEventFunc();

    [DllImport(PluginName, EntryPoint = "openvolumetric_open_external_console")]
    private static extern void openvolumetric_open_external_console();

    [DllImport(PluginName, EntryPoint = "openvolumetric_close_external_console")]
    private static extern void openvolumetric_close_external_console();

    [DllImport(PluginName, EntryPoint = "openvolumetric_init")]
    private static extern int openvolumetric_init(ref int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_quit")]
    private static extern void openvolumetric_quit(int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_set_time")]
    private static extern void openvolumetric_set_time(int id, double time);

    [DllImport(PluginName, EntryPoint = "openvolumetric_start_decoding")]
    private static extern int openvolumetric_start_decoding(int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_stop_decoding")]
    private static extern int openvolumetric_stop_decoding(int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_seek")]
    private static extern int openvolumetric_seek(int id, double time);

    [DllImport(PluginName, EntryPoint = "openvolumetric_load_video")]
    private static extern int openvolumetric_load_video(int id, string filename);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_last_error")]
    private static extern IntPtr openvolumetric_get_last_error(int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_last_presented_time")]
    private static extern double openvolumetric_get_last_presented_time(int id);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_video_details")]
    private static extern int openvolumetric_get_video_details(int id, ref int width, ref int height, ref double fps, ref double duration);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_audio_details")]
    private static extern int openvolumetric_get_audio_details(int id, ref int sample_rate, ref int channels);

    [DllImport(PluginName, EntryPoint = "openvolumetric_read_audio")]
    private static extern int openvolumetric_read_audio(int id, [Out] float[] samples, int sample_count);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_texture_pointers")]
    private static extern int openvolumetric_get_texture_pointers(int id, ref IntPtr Y, ref IntPtr U, ref IntPtr V);

    [DllImport(PluginName, EntryPoint = "openvolumetric_register_texture_pointers")]
    private static extern int openvolumetric_register_texture_pointers(
        int id, IntPtr Y, IntPtr U, IntPtr V);

    [DllImport(PluginName, EntryPoint = "openvolumetric_set_mesh_pointer")]
    private static extern int openvolumetric_set_mesh_pointer(int id, IntPtr index_buffer_handle, int index_size, IntPtr vertex_buffer_handle, int vertex_size);

    // The native instance identifier is valid until Dispose destroys it.
    private int m_instance_id = -1;

    // Stream metadata is populated once the combined MP4 opens.
    private int video_width = -1, video_height = -1;
    private double video_fps = 0.0, video_duration = 0.0;

    // Unity wraps native single-channel resources rather than copying planes
    // through managed memory every presentation.
    private Texture2D m_YTexture, m_UTexture, m_VTexture;

    private AudioClip m_audio_clip;
    /// <summary>Streaming clip backed by the native PCM ring.</summary>
    public AudioClip AudioClip
    {
        get { return m_audio_clip; }
    }

    /// <summary>Whether the opened MP4 contains a usable audio stream.</summary>
    public bool HasAudio
    {
        get { return m_audio_clip != null; }
    }

    /// <summary>The Unity mesh whose native buffers receive geometry.</summary>
    private Mesh m_mesh;
    public Mesh Mesh
    {
        get{return m_mesh;}
    }

    /// <summary>The renderer displaying the YUV material and decoded mesh.</summary>
    private MeshRenderer m_mesh_renderer;
    public MeshRenderer MeshRenderer
    {
        get {return m_mesh_renderer;}
    }


    // Used only to detect a genuine transition across the loop boundary.
    private int m_previous_frame;

    private bool m_debug;
    private bool m_disposed;

    /// <summary>Managed lifecycle state for the corresponding native player.</summary>
    public enum DecoderState
    {
        INIT_FAIL = -1,
        UNINITIALIZED,
        INITIALIZED,
        STARTED,
        STOPPED,
    };

    private DecoderState m_decoder_state = DecoderState.UNINITIALIZED;
    /// <summary>Current managed/native decoder lifecycle state.</summary>
    public DecoderState DecoderStatus
    {
        get{return m_decoder_state; }
        private set { m_decoder_state = value; }
    }

    // Latches when playback crosses the end until a seek or explicit reset.
    private bool m_loop = false;
    /// <summary>Whether playback crossed the current presentation end.</summary>
    public bool ContentLooped
    {
        get { return m_loop; }
    }

    /// <summary>Presentation duration in seconds.</summary>
    public double Duration
    {
        get { return video_duration; }
    }

    /// <summary>Nominal presentation frame rate.</summary>
    public double FrameRate
    {
        get { return video_fps; }
    }

    /// <summary>Most recent native error, or an empty string.</summary>
    public string LastError
    {
        get
        {
            if(m_instance_id < 0)
            {
                return String.Empty;
            }
            IntPtr message = openvolumetric_get_last_error(m_instance_id);
            return message == IntPtr.Zero
                ? String.Empty
                : Marshal.PtrToStringAnsi(message);
        }
    }

    /// <summary>Timestamp most recently uploaded by Unity's render thread.</summary>
    public double LastPresentedTime
    {
        get
        {
            return m_instance_id >= 0
                ? openvolumetric_get_last_presented_time(m_instance_id)
                : -1.0;
        }
    }

    /// <summary>
    /// Creates one native decoder instance and optionally opens its diagnostic
    /// console. Graphics and media resources are initialized separately.
    /// </summary>
    public OpenVolumetricDecoder(bool debug)
    {
        m_decoder_state = DecoderState.UNINITIALIZED;
        m_debug = debug;
        if (m_debug)
        {
            Debug.Log("OpenVolumetricDecoder - Opening External Console");
            openvolumetric_open_external_console();
        }
        if (openvolumetric_init(ref m_instance_id) == -1)
        {
            Debug.LogError("OpenVolumetricDecoder::init - failed to init");
            m_decoder_state = DecoderState.INIT_FAIL;
            return;
        }
        Debug.Log(String.Format("OpenVolumetricDecoder::init - instance id: {0}", m_instance_id));

        m_decoder_state = DecoderState.INITIALIZED;
    }

    /// <summary>
    /// Releases Unity objects and destroys the matching native instance.
    /// Safe to call repeatedly. Call this from Unity's main thread while its
    /// graphics device is still alive.
    /// </summary>
    public void Dispose()
    {
        if (m_disposed)
        {
            return;
        }
        m_disposed = true;
        m_YTexture = null;
        m_UTexture = null;
        m_VTexture = null;
        m_audio_clip = null;
        if(m_debug)
        {
            Debug.Log(String.Format(
                "OpenVolumetricDecoder::Dispose - Closing External Console - id: {0}",
                m_instance_id));
            openvolumetric_close_external_console();
        }
        if (m_instance_id >= 0)
        {
            Debug.Log(String.Format(
                "OpenVolumetricDecoder::Dispose - id: {0}",
                m_instance_id));
            openvolumetric_quit(m_instance_id);
            m_instance_id = -1;
        }

        GC.SuppressFinalize(this);
    }
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct Vertex
    {
        /// <summary>Constructs one interleaved native-compatible vertex.</summary>
        public Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
        {
            pos     = new Vector3(x, y, z);
            normal  = new Vector3(nx, ny, nz);
            uv      = new Vector3(u, v);
        }
        public Vector3 pos;
        public Vector3 normal;
        public Vector2 uv;
    }
    /// <summary>
    /// Allocates a fixed-capacity dynamic Unity mesh and registers its native
    /// index and vertex buffers with the plugin.
    /// </summary>
    public bool InitializeMesh()
    {
        if (m_decoder_state != DecoderState.INITIALIZED)
        {
            Debug.LogError("OpenVolumetricDecoder::init_mesh - failed to init");
            return false;
        }
        m_mesh = new Mesh();

        // Unity may rewrite these buffers every rendered presentation.
        m_mesh.MarkDynamic();
        const int vertex_count = 65535 ;
        const int index_count = 3 * 100000;
        var layout = new[]
        {
            new VertexAttributeDescriptor(VertexAttribute.Position,     VertexAttributeFormat.Float32, 3),
            new VertexAttributeDescriptor(VertexAttribute.Normal,       VertexAttributeFormat.Float32, 3),
            new VertexAttributeDescriptor(VertexAttribute.TexCoord0,    VertexAttributeFormat.Float32, 2)
        };
        m_mesh.SetVertexBufferParams(vertex_count, layout);

        // Initialize the full fixed-capacity buffer before exposing its
        // native handle to the plugin.
        NativeArray<Vertex> vert_buffer_data = new NativeArray<Vertex>(vertex_count, Allocator.Temp);
        for (int i = 0; i < vertex_count; i++)
        {
            vert_buffer_data[i] = new Vertex(0, 0, 0, 0, 0, 0, 0, 0);
        }
        m_mesh.SetVertexBufferData(vert_buffer_data, 0, 0, vertex_count);
        vert_buffer_data.Dispose();
        int[] tris_array = new int[index_count];
        for (int i = 0; i < index_count; i++)
        {
            tris_array[i] = 0;
        }

        m_mesh.indexFormat    = UnityEngine.Rendering.IndexFormat.UInt32;
        m_mesh.triangles      = tris_array;
        IntPtr index_buffer     = m_mesh.GetNativeIndexBufferPtr();
        IntPtr vertex_buffer    = m_mesh.GetNativeVertexBufferPtr(0);

        Debug.Log(String.Format("OpenVolumetricDecoder::init_mesh - {0} {1}", index_buffer, vertex_buffer));
        if (openvolumetric_set_mesh_pointer(m_instance_id, index_buffer, index_count, vertex_buffer, vertex_count ) == -1)
        {
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }

        Debug.Log("OpenVolumetricDecoder::init_mesh - Done");
        return true;
    }
    /// <summary>
    /// Opens the combined MP4 and creates the material and external YUV
    /// textures used by the selected graphics backend.
    /// </summary>
    public bool InitializeTexture(
        ref MeshRenderer meshRenderer,
        string videoFile)
    {
        m_mesh_renderer = meshRenderer;
        if (m_decoder_state != DecoderState.INITIALIZED)
        {
            Debug.LogError("OpenVolumetricDecoder::init_texture - failed to init");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        if(!LoadVideo(
            videoFile,
            ref video_width,
            ref video_height,
            ref video_fps,
            ref video_duration))
        {
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }   
        if(!InitializeTextures(
            ref m_mesh_renderer, video_width, video_height))
        {
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        openvolumetric_seek(m_instance_id, 0.0);
        
        return true;
    }
    /// <summary>
    /// Opens the native container and retrieves dimensions, rate, and duration.
    /// </summary>
    private bool LoadVideo(
        string filepath,
        ref int videoWidth,
        ref int videoHeight,
        ref double videoFps,
        ref double videoDuration)
    {
        if (openvolumetric_load_video(m_instance_id, filepath) == -1)
        {
            IntPtr errorPointer = openvolumetric_get_last_error(m_instance_id);
            string detail = errorPointer == IntPtr.Zero
                ? String.Empty
                : Marshal.PtrToStringAnsi(errorPointer);
            Debug.LogError("OpenVolumetricDecoder::init - failed to load video"
                + (String.IsNullOrEmpty(detail) ? String.Empty : ": " + detail));
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        if(openvolumetric_get_video_details(
            m_instance_id,
            ref videoWidth,
            ref videoHeight,
            ref videoFps,
            ref videoDuration) == -1)
        {
            Debug.LogError("OpenVolumetricDecoder::init - failed to get video details");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        Debug.Log( String.Format("OpenVolumetricDecoder::init - file:  {0}", filepath));
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init - width: {0}  height: {1}",
            videoWidth,
            videoHeight));
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init - fps: {0}  duration: {1}",
            videoFps,
            videoDuration));

        return true;
    }

    /// <summary>
    /// Creates a streaming AudioClip whose callback pulls synchronized PCM
    /// directly from the native decoder.
    /// </summary>
    public bool InitializeAudio()
    {
        int sampleRate = 0;
        int channels = 0;
        int result = openvolumetric_get_audio_details(
            m_instance_id, ref sampleRate, ref channels);
        if (result == 0)
        {
            Debug.Log("OpenVolumetricDecoder::init_audio - no audio stream");
            return true;
        }
        if (result < 0 || sampleRate <= 0 || channels <= 0)
        {
            Debug.LogError("OpenVolumetricDecoder::init_audio - invalid audio stream");
            return false;
        }

        int lengthSamples = Math.Max(
            1, (int)Math.Ceiling(video_duration * sampleRate));
        m_audio_clip = AudioClip.Create(
            "OpenVolumetricAudio",
            lengthSamples,
            channels,
            sampleRate,
            true,
            ReadAudio);
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init_audio - {0} Hz, {1} channels",
            sampleRate, channels));
        return m_audio_clip != null;
    }

    /// <summary>Fills Unity's audio request from the native PCM ring.</summary>
    private void ReadAudio(float[] samples)
    {
        int read = openvolumetric_read_audio(
            m_instance_id, samples, samples.Length);
        if (read < 0)
        {
            Array.Clear(samples, 0, samples.Length);
        }
    }
    /// <summary>
    /// Creates plane textures and completes the backend-specific native handle
    /// handshake required for render-thread uploads.
    /// </summary>
    private bool InitializeTextures(
        ref MeshRenderer meshRenderer,
        int width,
        int height)
    {
        try
        {
            meshRenderer.material =
                new Material(Shader.Find("OpenVolumetric/YUV2RGBA"));
        }
        catch
        {
            Debug.LogError("OpenVolumetricDecoder::init_textures - YUV2RGBA shader not found");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        IntPtr Y = new IntPtr();
        IntPtr U = new IntPtr();
        IntPtr V = new IntPtr();
        if(openvolumetric_get_texture_pointers(m_instance_id, ref Y, ref U, ref V) == -1)
        {
            Debug.LogError("OpenVolumetricDecoder::init_textures - Error Creating Textures");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
#if UNITY_ANDROID && !UNITY_EDITOR
        const TextureFormat planeFormat = TextureFormat.R8;
#else
        const TextureFormat planeFormat = TextureFormat.Alpha8;
#endif
        m_YTexture = Texture2D.CreateExternalTexture(width,     height,     planeFormat, false, true, Y);
        m_UTexture = Texture2D.CreateExternalTexture(width/2,   height/2,   planeFormat, false, true, U);
        m_VTexture = Texture2D.CreateExternalTexture(width/2,   height/2,   planeFormat, false, true, V);

#if UNITY_ANDROID && !UNITY_EDITOR
        // Unity tracks Vulkan resource state using its own native handles.
        // Register those handles after wrapping the plugin-owned VkImages.
        if (openvolumetric_register_texture_pointers(
                m_instance_id,
                m_YTexture.GetNativeTexturePtr(),
                m_UTexture.GetNativeTexturePtr(),
                m_VTexture.GetNativeTexturePtr()) == -1)
        {
            Debug.LogError("OpenVolumetricDecoder::init_textures - Error Registering Vulkan Textures");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
#endif
        meshRenderer.material.SetTexture("_YTex", m_YTexture);
        meshRenderer.material.SetTexture("_UTex", m_UTexture);
        meshRenderer.material.SetTexture("_VTex", m_VTexture);

        return true;
    }
    /// <summary>Starts the native demux and geometry worker threads.</summary>
    public bool StartDecoding()
    {
        if (m_decoder_state != DecoderState.INITIALIZED)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::start - Failed to start decoder for instance {0} - decoder not initialised", m_instance_id));
            return false;
        }
        if (openvolumetric_start_decoding(m_instance_id) == -1)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::start - Failed to start decoder for instance {0}",m_instance_id) );
            return false;
        }
        m_decoder_state = DecoderState.STARTED;

        return true;
    }
    /// <summary>Stops native workers while retaining initialized resources.</summary>
    public bool StopDecoding()
    {
        if(m_decoder_state == DecoderState.STOPPED)
        {    
            return false;
        }
        if(m_decoder_state != DecoderState.STARTED)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::stop - Failed to stop decoder for instance {0} - Decoder status has not started", m_instance_id));
            return false;
        }

        if(openvolumetric_stop_decoding(m_instance_id) == -1)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::stop - Failed to start decoder for instance {0}",m_instance_id) );
            return false;
        }

        m_decoder_state = DecoderState.STOPPED;

        return true;
    }

    /// <summary>Seeks the unified native pipeline to a time in seconds.</summary>
    public bool Seek(double time)
    {
        if (m_decoder_state != DecoderState.STARTED ||
            video_duration <= 0.0)
        {
            return false;
        }

        double target = Math.Max(0.0, Math.Min(time, video_duration));
        if (openvolumetric_seek(m_instance_id, target) == -1)
        {
            Debug.LogError(String.Format(
                "OpenVolumetricDecoder::seek - Failed to seek to {0:F3}",
                target));
            return false;
        }

        m_previous_frame = -1;
        m_loop = false;
        return true;
    }

    /// <summary>Clears the managed end-of-content latch for a new loop pass.</summary>
    public void ResetLoopFlag()
    {
        m_loop = false;
    }


    /// <summary>
    /// Supplies the desired presentation time and queues a render-thread plugin
    /// event that uploads matching texture and geometry.
    /// </summary>
    public void UpdatePresentation(double time)
    {
        double presentation_time = time % video_duration;
        int current_frame = (int)(presentation_time * video_fps);

        // Update frame bookkeeping and loop detection only when the encoded
        // frame changes. The render callback is still issued every Unity
        // update so a texture waiting for asynchronous Draco output can retry
        // the same timestamp instead of being abandoned on the next frame.
        if (m_previous_frame != current_frame)
        {
            // The managed playback clock owns loop detection. Reset every
            // native stream before requesting frame zero so no texture,
            // geometry, or audio from the previous pass can leak across.
            int total_frames = (int)(video_duration * video_fps);
            int loop_guard_frames = System.Math.Max(2, (int)video_fps * 2);
            bool crossed_loop_boundary =
                current_frame < m_previous_frame &&
                m_previous_frame >= total_frames - loop_guard_frames &&
                current_frame <= loop_guard_frames;
            if(crossed_loop_boundary)
            {
                m_loop = true;
                if(openvolumetric_seek(m_instance_id, 0.0) == -1)
                {
                    Debug.LogError("OpenVolumetricDecoder::update - failed to reset decoder at loop boundary");
                    return;
                }
            }
            m_previous_frame = current_frame;
        }

        // Video, geometry, and audio all use the same presentation time.
        openvolumetric_set_time(m_instance_id, presentation_time);
        GL.IssuePluginEvent(GetRenderEventFunc(), m_instance_id);
        m_mesh.RecalculateBounds();
    }

    /// <summary>Updates the YUV conversion material's correction parameters.</summary>
    public void SetColourCorrectionValues(
        float luminanceCorrection,
        float blueProjectionCorrection,
        float redProjectionCorrection)
    {
        if(m_decoder_state >= DecoderState.INITIALIZED)
        {
             m_mesh_renderer.material.SetFloat(
                 "_LuminanceCorrection", luminanceCorrection);
             m_mesh_renderer.material.SetFloat("_BlueProjectionCorrection", blueProjectionCorrection);
             m_mesh_renderer.material.SetFloat("_RedProjectionCorrection", redProjectionCorrection);
        }
    }


}

}
