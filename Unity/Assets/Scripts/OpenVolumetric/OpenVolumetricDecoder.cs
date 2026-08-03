using System.Collections;
using System.Collections.Generic;

using UnityEngine;
using UnityEngine.Rendering;
using Unity.Collections;

using System.Runtime.InteropServices;
using System;
using System.Text;

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
    private const string PluginName = "AudioPluginOpenVolumetricUnity";

    // Entry-point names intentionally mirror the stable native C ABI.
    [DllImport(PluginName, EntryPoint = "GetRenderEventFunc")]
    private static extern IntPtr GetRenderEventFunc();

    private enum NativeResult
    {
        Ok,
        InvalidArgument,
        InvalidHandle,
        UnsupportedFormat,
        CorruptData,
        NetworkFailure,
        Timeout,
        Cancelled,
        DecoderFailure,
        NotReady,
        InternalFailure
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeApiVersion
    {
        public uint StructSize;
        public uint Major;
        public uint Minor;
        public uint Patch;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeMediaInfo
    {
        public uint StructSize;
        public int Width;
        public int Height;
        public double FrameRate;
        public double Duration;
        public int HasAudio;
        public int AudioSampleRate;
        public int AudioChannels;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRuntimeSnapshot
    {
        public uint StructSize;
        public int InputState;
        public int Remote;
        public long ResourceSizeBytes;
        public ulong CachedBytes;
        public ulong DownloadedBytes;
        public ulong TransferThroughputBitsPerSecond;
        public ulong RequestCount;
        public ulong RecoveryCount;
        public int Fragmented;
        public long ActiveFragment;
        public ulong FragmentCount;
        public ulong CachedFragmentCount;
        public double AudioReadTime;
        public double AudioBufferedDuration;
        public ulong AudioUnderrunCount;
        public double LastPresentedTime;
        public double AdaptivePolicyThroughputBitsPerSecond;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct NativeAdaptiveSwitchSnapshot
    {
        public uint StructSize;
        public int State;
        public ulong Generation;
        public ulong SwitchCount;
        public double BoundaryTime;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string ActiveRepresentation;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string PendingRepresentation;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string Reason;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeCentroid
    {
        public uint StructSize;
        public float X;
        public float Y;
        public float Z;
    }

    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_get_api_version(
        ref NativeApiVersion version);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_create(
        ref IntPtr player);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_destroy(IntPtr player);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_render_event_id(
        IntPtr player, ref int eventId);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_set_time(
        IntPtr player, double time);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_open(
        IntPtr player, string resource);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_start(IntPtr player);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_stop(IntPtr player);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_seek(
        IntPtr player, double time);
    [DllImport(PluginName, CharSet = CharSet.Ansi)]
    private static extern NativeResult openvolumetric_player_get_error(
        IntPtr player, StringBuilder buffer, uint capacity,
        ref uint requiredCapacity, ref NativeResult category);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_media_info(
        IntPtr player, ref NativeMediaInfo info);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_runtime_snapshot(
        IntPtr player, ref NativeRuntimeSnapshot snapshot);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_adaptive_switch_snapshot(
        IntPtr player, ref NativeAdaptiveSwitchSnapshot snapshot);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_centroid(
        IntPtr player, ref NativeCentroid centroid);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_configure_adaptive(
        IntPtr player, string representationId);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_clear_adaptive_policy(
        IntPtr player);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_add_adaptive_representation(
        IntPtr player, string representationId, string resource, ulong bandwidth);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_update_adaptive_policy(
        IntPtr player, double now, double presentationTime, double duration,
        double segmentDuration, ref int action);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_request_adaptive_index(
        IntPtr player, ulong targetIndex, double now, double presentationTime,
        double duration, double segmentDuration, ref int action);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_schedule_dsp_audio(
        IntPtr player, ulong dspStartTick, double mediaStartTime);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_stop_dsp_audio(
        IntPtr player);

    [DllImport(PluginName, EntryPoint = "openvolumetric_get_dsp_audio_time")]
    private static extern double openvolumetric_get_dsp_audio_time();

    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_get_texture_pointers(
        IntPtr player, ref IntPtr y, ref IntPtr u, ref IntPtr v);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_register_texture_pointers(
        IntPtr player, IntPtr y, IntPtr u, IntPtr v);
    [DllImport(PluginName)]
    private static extern NativeResult openvolumetric_player_set_mesh_buffers(
        IntPtr player, IntPtr indexBuffer, int indexCount,
        IntPtr vertexBuffer, int vertexCount);

    private IntPtr m_player = IntPtr.Zero;
    private int m_render_event_id = -1;

    // Stream metadata is populated once the combined MP4 opens.
    private int video_width = -1, video_height = -1;
    private double video_fps = 0.0, video_duration = 0.0;

    // Unity wraps native single-channel resources rather than copying planes
    // through managed memory every presentation.
    private Texture2D m_YTexture, m_UTexture, m_VTexture;

    private AudioClip m_audio_clip;
    private int m_audio_sample_rate;
    /// <summary>Silent carrier that routes the native effect through Unity.</summary>
    public AudioClip AudioClip
    {
        get { return m_audio_clip; }
    }

    /// <summary>Whether the opened MP4 contains a usable audio stream.</summary>
    public bool HasAudio
    {
        get { return m_audio_clip != null; }
    }

    /// <summary>Latest media timestamp processed inside Unity's DSP graph.</summary>
    public double DspAudioTime
    {
        get
        {
            return HasAudio
                ? openvolumetric_get_dsp_audio_time()
                : -1.0;
        }
    }

    /// <summary>Immutable snapshot of native PCM readiness and consumption.</summary>
    public struct AudioBufferInfo
    {
        public double ReadTime;
        public double BufferedDuration;
        public ulong UnderrunCount;

        /// <summary>Whether decoded PCM covers the requested startup duration.</summary>
        public bool HasPreroll(double requiredDuration)
        {
            return BufferedDuration >= requiredDuration;
        }
    }

    /// <summary>Current timestamp and occupancy of the native PCM ring.</summary>
    public AudioBufferInfo CurrentAudioBufferInfo
    {
        get
        {
            AudioBufferInfo info = new AudioBufferInfo();
            NativeRuntimeSnapshot snapshot;
            if(HasAudio && TryGetRuntimeSnapshot(out snapshot))
            {
                info.ReadTime = snapshot.AudioReadTime;
                info.BufferedDuration = snapshot.AudioBufferedDuration;
                info.UnderrunCount = snapshot.AudioUnderrunCount;
            }
            return info;
        }
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
            if(m_player == IntPtr.Zero)
            {
                return String.Empty;
            }
            uint required = 0;
            NativeResult category = NativeResult.Ok;
            openvolumetric_player_get_error(
                m_player, null, 0, ref required, ref category);
            if(required <= 1)
            {
                return String.Empty;
            }
            StringBuilder message = new StringBuilder((int)required);
            return openvolumetric_player_get_error(
                m_player, message, required, ref required, ref category) ==
                NativeResult.Ok ? message.ToString() : String.Empty;
        }
    }

    /// <summary>Timestamp most recently uploaded by Unity's render thread.</summary>
    public double LastPresentedTime
    {
        get
        {
            NativeRuntimeSnapshot snapshot;
            return TryGetRuntimeSnapshot(out snapshot)
                ? snapshot.LastPresentedTime : -1.0;
        }
    }

    /// <summary>
    /// Returns the local-space vertex centroid of the latest uploaded mesh.
    /// </summary>
    public bool TryGetGeometryCentroid(out Vector3 centroid)
    {
        centroid = Vector3.zero;
        if(m_player == IntPtr.Zero)
        {
            return false;
        }
        NativeCentroid native = new NativeCentroid
        {
            StructSize = (uint)Marshal.SizeOf<NativeCentroid>()
        };
        if(openvolumetric_player_get_centroid(m_player, ref native) !=
            NativeResult.Ok)
        {
            return false;
        }
        centroid = new Vector3(native.X, native.Y, native.Z);
        return true;
    }

    /// <summary>Immutable native input/cache counter snapshot.</summary>
    public struct BufferInfo
    {
        public BufferState State;
        public bool IsRemote;
        public long ResourceSizeBytes;
        public ulong CachedBytes;
        public ulong DownloadedBytes;
        public ulong TransferThroughputBitsPerSecond;
        public ulong RequestCount;
        public ulong RecoveryCount;
        public bool IsFragmented;
        public long ActiveFragment;
        public ulong FragmentCount;
        public ulong CachedFragmentCount;
    }

    /// <summary>Native input transport and bounded-cache lifecycle.</summary>
    public enum BufferState
    {
        Opening = 0,
        Ready = 1,
        Rebuffering = 2,
        Error = 3,
        Cancelled = 4,
        Ended = 5
    }

    /// <summary>Lifecycle of one generation-safe representation switch.</summary>
    public enum AdaptiveSwitchState
    {
        Stable,
        Preparing,
        Ready,
        Failed
    }

    /// <summary>Immutable native adaptive-transition snapshot.</summary>
    public struct AdaptiveSwitchInfo
    {
        public AdaptiveSwitchState State;
        public ulong Generation;
        public ulong SwitchCount;
        public double BoundaryTime;
        public string ActiveRepresentation;
        public string PendingRepresentation;
        public string Reason;
    }

    /// <summary>Associates the opened native session with its manifest entry.</summary>
    public bool ConfigureAdaptiveRepresentation(string representationId)
    {
        return m_player != IntPtr.Zero &&
            openvolumetric_player_configure_adaptive(
                m_player, representationId) == NativeResult.Ok;
    }

    /// <summary>Clears the shared native adaptive-policy ladder.</summary>
    public void ClearAdaptivePolicy()
    {
        if(m_player != IntPtr.Zero)
        {
            openvolumetric_player_clear_adaptive_policy(m_player);
        }
    }

    /// <summary>Adds one capability-compatible representation to the policy.</summary>
    public void AddAdaptivePolicyRepresentation(
        string id, string resource, ulong bandwidth)
    {
        if(m_player != IntPtr.Zero)
        {
            openvolumetric_player_add_adaptive_representation(
                m_player, id, resource, bandwidth);
        }
    }

    /// <summary>Evaluates the engine-neutral policy using native diagnostics.</summary>
    public int UpdateAdaptivePolicy(
        double now, double presentationTime, double duration,
        double segmentDuration)
    {
        int action = -1;
        return m_player != IntPtr.Zero &&
            openvolumetric_player_update_adaptive_policy(
                m_player, now, presentationTime, duration, segmentDuration,
                ref action) == NativeResult.Ok ? action : -1;
    }

    /// <summary>Requests a ladder entry while bypassing automatic backoff.</summary>
    public int RequestAdaptivePolicyIndex(
        ulong index, double now, double presentationTime, double duration,
        double segmentDuration)
    {
        int action = -1;
        return m_player != IntPtr.Zero &&
            openvolumetric_player_request_adaptive_index(
                m_player, index, now, presentationTime, duration,
                segmentDuration, ref action) == NativeResult.Ok ? action : -1;
    }

    public double AdaptivePolicyThroughputBitsPerSecond
    {
        get
        {
            NativeRuntimeSnapshot snapshot;
            return TryGetRuntimeSnapshot(out snapshot)
                ? snapshot.AdaptivePolicyThroughputBitsPerSecond : 0.0;
        }
    }

    /// <summary>Current generation-safe adaptive transition state.</summary>
    public AdaptiveSwitchInfo CurrentAdaptiveSwitchInfo
    {
        get
        {
            AdaptiveSwitchInfo info = new AdaptiveSwitchInfo();
            if(m_player == IntPtr.Zero)
            {
                return info;
            }
            NativeAdaptiveSwitchSnapshot snapshot =
                new NativeAdaptiveSwitchSnapshot
                {
                    StructSize =
                        (uint)Marshal.SizeOf<NativeAdaptiveSwitchSnapshot>()
                };
            if(openvolumetric_player_get_adaptive_switch_snapshot(
                m_player, ref snapshot) != NativeResult.Ok)
            {
                return info;
            }
            info.State = (AdaptiveSwitchState)snapshot.State;
            info.Generation = snapshot.Generation;
            info.SwitchCount = snapshot.SwitchCount;
            info.BoundaryTime = snapshot.BoundaryTime;
            info.ActiveRepresentation = snapshot.ActiveRepresentation ?? "";
            info.PendingRepresentation = snapshot.PendingRepresentation ?? "";
            info.Reason = snapshot.Reason ?? "";
            return info;
        }
    }

    /// <summary>Current transport and bounded-cache diagnostics.</summary>
    public BufferInfo InputBufferInfo
    {
        get
        {
            BufferInfo info = new BufferInfo();
            NativeRuntimeSnapshot snapshot;
            if(!TryGetRuntimeSnapshot(out snapshot))
            {
                return info;
            }
            info.State = (BufferState)snapshot.InputState;
            info.IsRemote = snapshot.Remote != 0;
            info.ResourceSizeBytes = snapshot.ResourceSizeBytes;
            info.CachedBytes = snapshot.CachedBytes;
            info.DownloadedBytes = snapshot.DownloadedBytes;
            info.TransferThroughputBitsPerSecond =
                snapshot.TransferThroughputBitsPerSecond;
            info.RequestCount = snapshot.RequestCount;
            info.RecoveryCount = snapshot.RecoveryCount;
            info.IsFragmented = snapshot.Fragmented != 0;
            info.ActiveFragment = snapshot.ActiveFragment;
            info.FragmentCount = snapshot.FragmentCount;
            info.CachedFragmentCount = snapshot.CachedFragmentCount;
            return info;
        }
    }

    private bool TryGetRuntimeSnapshot(out NativeRuntimeSnapshot snapshot)
    {
        snapshot = new NativeRuntimeSnapshot
        {
            StructSize = (uint)Marshal.SizeOf<NativeRuntimeSnapshot>()
        };
        return m_player != IntPtr.Zero &&
            openvolumetric_player_get_runtime_snapshot(
                m_player, ref snapshot) == NativeResult.Ok;
    }

    /// <summary>
    /// Creates one native decoder instance. Graphics and media resources are
    /// initialized separately.
    /// </summary>
    public OpenVolumetricDecoder()
    {
        m_decoder_state = DecoderState.UNINITIALIZED;
        NativeApiVersion version = new NativeApiVersion
        {
            StructSize = (uint)Marshal.SizeOf<NativeApiVersion>()
        };
        if(openvolumetric_get_api_version(ref version) != NativeResult.Ok ||
            version.Major != 1)
        {
            Debug.LogError("OpenVolumetricDecoder - incompatible native API");
            m_decoder_state = DecoderState.INIT_FAIL;
            return;
        }
        NativeResult createResult = openvolumetric_player_create(ref m_player);
        if (createResult != NativeResult.Ok)
        {
            Debug.LogError(String.Format(
                "OpenVolumetricDecoder::init - native player creation failed " +
                "({0}); unsupported graphics API: {1}.",
                createResult,
                SystemInfo.graphicsDeviceType));
            m_decoder_state = DecoderState.INIT_FAIL;
            return;
        }
        NativeResult eventResult = openvolumetric_player_get_render_event_id(
            m_player, ref m_render_event_id);
        if (eventResult != NativeResult.Ok)
        {
            Debug.LogError(String.Format(
                "OpenVolumetricDecoder::init - render event creation failed ({0})",
                eventResult));
            openvolumetric_player_destroy(m_player);
            m_player = IntPtr.Zero;
            m_decoder_state = DecoderState.INIT_FAIL;
            return;
        }
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init - render event id: {0}",
            m_render_event_id));

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
        if (m_player != IntPtr.Zero)
        {
            Debug.Log(String.Format(
                "OpenVolumetricDecoder::Dispose - render event id: {0}",
                m_render_event_id));
            openvolumetric_player_destroy(m_player);
            m_player = IntPtr.Zero;
            m_render_event_id = -1;
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
        if (openvolumetric_player_set_mesh_buffers(
            m_player, index_buffer, index_count, vertex_buffer, vertex_count) !=
            NativeResult.Ok)
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
        openvolumetric_player_seek(m_player, 0.0);
        
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
        NativeResult openResult = openvolumetric_player_open(m_player, filepath);
        if (openResult != NativeResult.Ok)
        {
            string detail = LastError;
            Debug.LogError("OpenVolumetricDecoder::init - failed to load video"
                + " (" + openResult + ")"
                + (String.IsNullOrEmpty(detail) ? String.Empty : ": " + detail));
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        NativeMediaInfo info = new NativeMediaInfo
        {
            StructSize = (uint)Marshal.SizeOf<NativeMediaInfo>()
        };
        if(openvolumetric_player_get_media_info(m_player, ref info) !=
            NativeResult.Ok)
        {
            Debug.LogError("OpenVolumetricDecoder::init - failed to get video details");
            m_decoder_state = DecoderState.INIT_FAIL;
            return false;
        }
        videoWidth = info.Width;
        videoHeight = info.Height;
        videoFps = info.FrameRate;
        videoDuration = info.Duration;
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init - source: {0}",
            filepath.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
                filepath.StartsWith("https://", StringComparison.OrdinalIgnoreCase)
                ? "remote HTTP source"
                : filepath));
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
    /// Creates the silent carrier clip that activates the native DSP effect.
    /// </summary>
    public bool InitializeAudio()
    {
        NativeMediaInfo info = new NativeMediaInfo
        {
            StructSize = (uint)Marshal.SizeOf<NativeMediaInfo>()
        };
        if(openvolumetric_player_get_media_info(m_player, ref info) !=
            NativeResult.Ok)
        {
            Debug.LogError("OpenVolumetricDecoder::init_audio - media info unavailable");
            return false;
        }
        if (info.HasAudio == 0)
        {
            Debug.Log("OpenVolumetricDecoder::init_audio - no audio stream");
            return true;
        }
        int sampleRate = info.AudioSampleRate;
        int channels = info.AudioChannels;
        if (sampleRate <= 0 || channels <= 0)
        {
            Debug.LogError("OpenVolumetricDecoder::init_audio - invalid audio stream");
            return false;
        }

        int lengthSamples = Math.Max(
            1, (int)Math.Ceiling(video_duration * sampleRate));
        m_audio_sample_rate = sampleRate;
        m_audio_clip = AudioClip.Create(
            "OpenVolumetricDspCarrier",
            lengthSamples,
            channels,
            sampleRate,
            false);
        Debug.Log(String.Format(
            "OpenVolumetricDecoder::init_audio - {0} Hz, {1} channels",
            sampleRate, channels));
        return m_audio_clip != null;
    }

    /// <summary>Arms native PCM generation at an absolute Unity DSP time.</summary>
    public bool ScheduleDspAudio(double dspTime, double mediaStartTime)
    {
        int sampleRate = AudioSettings.outputSampleRate > 0
            ? AudioSettings.outputSampleRate
            : System.Math.Max(1, m_audio_sample_rate);
        ulong dspTick = (ulong)System.Math.Max(
            0.0,
            System.Math.Round(dspTime * sampleRate));
        return openvolumetric_player_schedule_dsp_audio(
            m_player, dspTick, mediaStartTime) == NativeResult.Ok;
    }

    /// <summary>Stops native DSP consumption before a timeline mutation.</summary>
    public void StopDspAudio()
    {
        if(HasAudio && m_player != IntPtr.Zero)
        {
            openvolumetric_player_stop_dsp_audio(m_player);
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
        if(openvolumetric_player_get_texture_pointers(
            m_player, ref Y, ref U, ref V) != NativeResult.Ok)
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
        if (openvolumetric_player_register_texture_pointers(
                m_player,
                m_YTexture.GetNativeTexturePtr(),
                m_UTexture.GetNativeTexturePtr(),
                m_VTexture.GetNativeTexturePtr()) != NativeResult.Ok)
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
            Debug.LogError(String.Format("OpenVolumetricDecoder::start - Failed to start decoder for render event {0} - decoder not initialised", m_render_event_id));
            return false;
        }
        if (openvolumetric_player_start(m_player) != NativeResult.Ok)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::start - Failed to start decoder for render event {0}", m_render_event_id));
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
            Debug.LogError(String.Format("OpenVolumetricDecoder::stop - Failed to stop decoder for render event {0} - Decoder status has not started", m_render_event_id));
            return false;
        }

        if(openvolumetric_player_stop(m_player) != NativeResult.Ok)
        {
            Debug.LogError(String.Format("OpenVolumetricDecoder::stop - Failed to stop decoder for render event {0}", m_render_event_id));
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
        StopDspAudio();
        if (openvolumetric_player_seek(m_player, target) != NativeResult.Ok)
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
                StopDspAudio();
                if(openvolumetric_player_seek(m_player, 0.0) != NativeResult.Ok)
                {
                    Debug.LogError("OpenVolumetricDecoder::update - failed to reset decoder at loop boundary");
                    return;
                }
            }
            m_previous_frame = current_frame;
        }

        // Video, geometry, and audio all use the same presentation time.
        openvolumetric_player_set_time(m_player, presentation_time);
        GL.IssuePluginEvent(GetRenderEventFunc(), m_render_event_id);
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
