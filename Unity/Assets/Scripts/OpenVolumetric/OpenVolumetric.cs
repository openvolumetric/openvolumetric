using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Serialization;

namespace OpenVolumetric
{

/// <summary>
/// Unity component that opens and plays a combined volumetric MP4.
///
/// Unity's DSP clock provides the playback timeline. Each update selects a
/// presentation frame; the native render callback publishes its matching
/// texture and geometry while the native DSP effect pulls decoded PCM.
/// </summary>
public class OpenVolumetric : MonoBehaviour
{
    /// <summary>Startup representation choice for adaptive manifests.</summary>
    public enum AdaptiveQuality
    {
        Auto,
        Low,
        High
    }

    /// <summary>Combined OpenVolumetric MP4 beneath StreamingAssets.</summary>
    [Header("Volumetric Video Input")]
    [Tooltip("Volumetric video file containing geometry, texture, and audio")]
    public string videoFilename;
    /// <summary>Optional HTTP(S) MP4 URL; takes precedence over videoFilename.</summary>
    [Tooltip("Optional HTTP(S) URL. When set, this takes precedence over the StreamingAssets filename.")]
    public string videoUrl;
    /// <summary>Interpret the selected local file or URL as manifest.json.</summary>
    [Tooltip("Load an adaptive manifest and select one representation before playback starts.")]
    public bool useAdaptiveManifest;
    /// <summary>Manual quality or a conservative HTTP startup measurement.</summary>
    [Tooltip("Startup adaptive quality. For HTTP, Auto measures initial throughput and requires safety headroom before selecting High.")]
    public AdaptiveQuality adaptiveQuality = AdaptiveQuality.Auto;
    /// <summary>Optional maximum texture dimension for Auto; zero uses the platform profile.</summary>
    [Header("Adaptive Capability Overrides")]
    [Tooltip("Maximum texture width and height for Auto selection. Zero uses the platform default.")]
    [Min(0)]
    public int adaptiveMaximumTextureDimension;
    /// <summary>Optional Auto texture bitrate ceiling in megabits per second.</summary>
    [Tooltip("Maximum texture bitrate in Mbps. Zero uses the platform default.")]
    [Min(0.0F)]
    public float adaptiveMaximumTextureBitrateMbps;
    /// <summary>Optional Auto geometry bitrate ceiling in megabits per second.</summary>
    [Tooltip("Maximum geometry bitrate in Mbps. Zero uses the platform default.")]
    [Min(0.0F)]
    public float adaptiveMaximumGeometryBitrateMbps;
    /// <summary>Optional Auto aggregate bitrate ceiling in megabits per second.</summary>
    [Tooltip("Maximum combined representation bitrate in Mbps. Zero uses the platform default.")]
    [Min(0.0F)]
    public float adaptiveMaximumBandwidthMbps;
    /// <summary>Allow Auto HTTP playback to change quality at fragment boundaries.</summary>
    [Tooltip("Permit automatic quality changes during HTTP playback. Requires Auto quality and at least two eligible representations.")]
    public bool enableLiveAdaptiveSwitching = true;
    /// <summary>Additive correction applied to the decoded Y channel.</summary>
    [Header("Texture Settings")]
    [Tooltip("Luminance Correction - Y")]
    [Range(-0.2F, 0.2F)]
    [FormerlySerializedAs("luminaceCorrection")]
    public float luminanceCorrection = 0.0F;
    /// <summary>Additive correction applied to the decoded U channel.</summary>
    [Tooltip("Chrominance Correction - Blue Projection - U")]
    [Range(-0.2F, 0.2F)]
    public float blueProjectionCorrection=0.0F;
    /// <summary>Additive correction applied to the decoded V channel.</summary>
    [Tooltip("Chrominance Correction - Red Projection - V")]
    [Range(-0.2F, 0.2F)]
    public float redProjectionCorrection=0.0F;
    /// <summary>Whether playback seeks to the beginning at end of stream.</summary>
    [Header("Playback Settings")]
    [Tooltip("When enabled the content will loop")]
    public bool enableLoop = false;
    /// <summary>Whether a caller must schedule playback explicitly.</summary>
    [Tooltip("Enables playback to be started via a script ")]
    public bool enableScriptedStart;
    /// <summary>Whether runtime diagnostics and controls are attached.</summary>
    [Tooltip("Show the keyboard/controller developer overlay")]
    public bool enableDeveloperOverlay = true;
    /// <summary>Whether native diagnostic console output is enabled.</summary>
    [Header("Debug Settings")]
    [Tooltip("Enable debug, will launch an external console")]
    public bool debug = false;
    /// <summary>Write periodic adaptive playback measurements as CSV.</summary>
    [Header("Adaptive Evaluation")]
    [Tooltip("Record transport, buffering, and quality-switch metrics in Application.persistentDataPath.")]
    public bool recordAdaptiveMetrics;
    [Tooltip("CSV filename written beneath Application.persistentDataPath.")]
    public string adaptiveMetricsFileName = "openvolumetric-adaptive-metrics.csv";
    [Range(0.05F, 5.0F)]
    [Tooltip("Time in seconds between adaptive metric samples.")]
    public float adaptiveMetricsInterval = 0.25F;
    private OpenVolumetricDecoder m_decoder;
    private AudioSource m_audio_source;
    private readonly OpenVolumetricPlaybackClock m_clock =
        new OpenVolumetricPlaybackClock();
    private double m_audio_preroll_duration;
    private bool m_waiting_for_audio_preroll;
    private readonly OpenVolumetricRecoveryPolicy m_recovery_policy =
        new OpenVolumetricRecoveryPolicy();
    private bool m_decoder_recovering;
    private double m_decoder_recovery_target;
    /// <summary>Managed playback states exposed to scripts and diagnostics.</summary>
    public enum PlaybackState
    {
        INIT_FAIL = -1,
        UNINITIALISED,
        INITIALISED,
        SCHEDULED,
        PAUSED,
        PREROLLING,
        REBUFFERING,
        STOPPED,
        PLAYING,
    };
    private PlaybackState m_playback_state = PlaybackState.UNINITIALISED;
    private double m_playback_position
    {
        get { return m_clock.Position; }
        set { m_clock.Position = value; }
    }
    private bool m_network_rebuffering;
    private bool m_resume_after_network_recovery;
    private double m_network_recovery_target;
    private bool m_network_decoder_recovery;
    private double m_network_settle_until = -1.0;
    private bool m_waiting_for_initial_presentation;
    private bool m_play_after_initial_presentation;
    private string m_selected_representation_id = "";
    private ulong m_adaptive_throughput_bps;
    private string m_adaptive_decision_reason = "";
    private readonly List<OpenVolumetricSourceResolver.Representation>
        m_adaptive_representations =
            new List<OpenVolumetricSourceResolver.Representation>();
    private double m_adaptive_segment_duration;
    private double m_adaptive_smoothed_throughput_bps;
    private readonly OpenVolumetricAdaptiveMetrics m_adaptive_metrics =
        new OpenVolumetricAdaptiveMetrics();

    /// <summary>Current managed playback state.</summary>
    public PlaybackState State { get { return m_playback_state; } }
    /// <summary>Whether the shared playback clock is currently advancing.</summary>
    public bool IsPlaying { get { return m_playback_state == PlaybackState.PLAYING; } }
    /// <summary>Duration of the opened presentation in seconds.</summary>
    public double Duration { get { return m_decoder != null ? m_decoder.Duration : 0.0; } }
    /// <summary>Most recent native decoder error, or an empty string.</summary>
    public string LastError
    {
        get { return m_decoder != null ? m_decoder.LastError : string.Empty; }
    }
    /// <summary>Current native input and bounded-cache diagnostics.</summary>
    public OpenVolumetricDecoder.BufferInfo InputBufferInfo
    {
        get
        {
            return m_decoder != null
                ? m_decoder.InputBufferInfo
                : new OpenVolumetricDecoder.BufferInfo();
        }
    }
    /// <summary>Whether the opened presentation has native DSP audio.</summary>
    public bool NativeDspAudioEnabled
    {
        get { return m_decoder != null && m_decoder.HasAudio; }
    }
    /// <summary>Media time most recently processed by Unity's DSP callback.</summary>
    public double NativeDspAudioTime
    {
        get { return m_decoder != null ? m_decoder.DspAudioTime : -1.0; }
    }
    /// <summary>AudioSource carrying the decoded native DSP stream.</summary>
    public AudioSource AudioOutput { get { return m_audio_source; } }
    /// <summary>Representation selected from the adaptive manifest.</summary>
    public string SelectedRepresentationId
    {
        get { return m_selected_representation_id; }
    }
    /// <summary>Bandwidth measured by the bounded HTTP Auto probe, in bits per second.</summary>
    public ulong AdaptiveThroughputBitsPerSecond
    {
        get { return m_adaptive_throughput_bps; }
    }
    /// <summary>Explanation of the current adaptive startup decision.</summary>
    public string AdaptiveDecisionReason
    {
        get { return m_adaptive_decision_reason; }
    }
    /// <summary>Current live representation transition diagnostics.</summary>
    public OpenVolumetricDecoder.AdaptiveSwitchInfo AdaptiveSwitchStatus
    {
        get
        {
            return m_decoder != null
                ? m_decoder.CurrentAdaptiveSwitchInfo
                : new OpenVolumetricDecoder.AdaptiveSwitchInfo();
        }
    }

    /// <summary>Returns the latest decoded geometry centroid in local space.</summary>
    public bool TryGetGeometryCentroid(out Vector3 centroid)
    {
        if(m_decoder != null)
        {
            return m_decoder.TryGetGeometryCentroid(out centroid);
        }
        centroid = Vector3.zero;
        return false;
    }
    /// <summary>Current playback position in seconds.</summary>
    public double CurrentTime
    {
        get
        {
            if (m_playback_state == PlaybackState.PLAYING ||
                m_playback_state == PlaybackState.SCHEDULED)
            {
                double time = System.Math.Max(0.0, m_playback_position);
                if(enableLoop && Duration > 0.0)
                {
                    time %= Duration;
                }
                return time;
            }
            return m_playback_position;
        }
    }

    /// <summary>
    /// Resolves the combined MP4, creates Unity render/audio resources, and
    /// starts the native decode workers.
    /// </summary>
    IEnumerator Start()
    {
        MeshFilter mesh_filter = gameObject.AddComponent<MeshFilter>();
        MeshRenderer mesh_renderer = gameObject.AddComponent<MeshRenderer>();
        m_decoder = new OpenVolumetricDecoder(debug);
             
        OpenVolumetricSourceResolver resolver =
            new OpenVolumetricSourceResolver();
        OpenVolumetricSourceResolver.Result source = null;
        yield return resolver.Resolve(
            new OpenVolumetricSourceResolver.Request
            {
                Filename = videoFilename,
                Url = videoUrl,
                UseAdaptiveManifest = useAdaptiveManifest,
                Quality = adaptiveQuality,
                MaximumTextureDimension = adaptiveMaximumTextureDimension,
                MaximumTextureBitrateMbps =
                    adaptiveMaximumTextureBitrateMbps,
                MaximumGeometryBitrateMbps =
                    adaptiveMaximumGeometryBitrateMbps,
                MaximumBandwidthMbps = adaptiveMaximumBandwidthMbps
            },
            result => source = result);
        if(source == null || !source.Succeeded)
        {
            Debug.LogError(
                "OpenVolumetric::Start - " +
                (source != null ? source.Error : "Source resolution failed"));
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }
        m_selected_representation_id = source.RepresentationId;
        m_adaptive_throughput_bps =
            source.MeasuredThroughputBitsPerSecond;
        m_adaptive_decision_reason = source.DecisionReason;
        m_adaptive_segment_duration = source.SegmentDuration;
        m_adaptive_representations.Clear();
        m_adaptive_representations.AddRange(source.Representations);

        if(!m_decoder.InitializeTexture(
            ref mesh_renderer,
            source.PlayableResource))
        {
            Debug.LogError("OpenVolumetric::Start - Failed to init OpenVolumetricDecoder");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }
        if(useAdaptiveManifest)
        {
            m_decoder.ConfigureAdaptiveRepresentation(
                m_selected_representation_id);
            m_decoder.ClearAdaptivePolicy();
            foreach(OpenVolumetricSourceResolver.Representation representation in
                m_adaptive_representations)
            {
                m_decoder.AddAdaptivePolicyRepresentation(
                    representation.Id,
                    representation.Resource,
                    representation.Bandwidth);
            }
        }

        if(!m_decoder.InitializeMesh())
        {
            Debug.LogError("OpenVolumetric::Start - Failed to init geometry");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }

        // Assign the native-backed mesh to the mesh filter.
        mesh_filter.mesh = m_decoder.Mesh;

        if(!m_decoder.InitializeAudio())
        {
            Debug.LogError("OpenVolumetric::Start - Failed to initialise audio");
        }
        else if(m_decoder.HasAudio)
        {
            OpenVolumetricGeometryAudioFollower follower =
                gameObject.GetComponent<OpenVolumetricGeometryAudioFollower>();
            if(follower != null)
            {
                GameObject audioObject = new GameObject(
                    "OpenVolumetric Spatial Audio");
                audioObject.transform.SetParent(transform, false);
                m_audio_source = audioObject.AddComponent<AudioSource>();
            }
            else
            {
                m_audio_source = gameObject.GetComponent<AudioSource>();
                if(m_audio_source == null)
                {
                    m_audio_source = gameObject.AddComponent<AudioSource>();
                }
            }
            m_audio_source.playOnAwake = false;
            m_audio_source.loop = enableLoop;
            m_audio_source.spatialBlend = 0.0F;
            m_audio_source.spatialize = true;
            m_audio_source.clip = m_decoder.AudioClip;

            // Require enough native PCM for Unity's complete DSP queue plus a
            // small scheduling margin before either timeline is released.
            int dspBufferLength;
            int dspBufferCount;
            AudioSettings.GetDSPBufferSize(
                out dspBufferLength, out dspBufferCount);
            int outputRate = AudioSettings.outputSampleRate;
            if(outputRate <= 0)
            {
                // Android can report zero while its audio device is still
                // starting. The carrier uses the decoded PCM sample rate.
                outputRate = m_decoder.AudioClip.frequency;
            }
            double configuredQueueDuration =
                (double)(dspBufferLength * dspBufferCount) /
                System.Math.Max(1, outputRate);
            m_audio_preroll_duration = System.Math.Min(
                1.0,
                System.Math.Max(0.15, configuredQueueDuration + 0.1));
            Debug.Log(string.Format(
                "OpenVolumetric - audio preroll {0:F3}s " +
                "(DSP {1} x {2}, {3} Hz)",
                m_audio_preroll_duration,
                dspBufferLength,
                dspBufferCount,
                outputRate));
        }

        if(enableDeveloperOverlay)
        {
            OpenVolumetricDeveloperOverlay.Attach(this);
        }

        m_decoder.SetColourCorrectionValues(
            luminanceCorrection,
            blueProjectionCorrection,
            redProjectionCorrection);
        if(!m_decoder.StartDecoding())
        {
            Debug.LogError("OpenVolumetric::Start - Failed to start decoding");
            m_playback_state = PlaybackState.INIT_FAIL;
            yield break;
        }

        // A remote source may need several range requests before texture and
        // geometry for the first timestamp are both available. Hold the
        // timeline at zero until that complete presentation has reached the
        // render thread; otherwise the advancing clock continually discards
        // late startup frames and may never establish visual playback.
        m_play_after_initial_presentation =
            !enableScriptedStart || m_clock.HasScheduledStart;
        m_waiting_for_initial_presentation = true;
        m_playback_position = 0.0;
        m_playback_state = PlaybackState.PREROLLING;
    }

    /// <summary>
    /// Advances the shared DSP-clock timeline and submits its presentation
    /// target to the native render callback once per Unity frame.
    /// </summary>
    void Update()
    {
        if(m_decoder != null)
        {
            RecordAdaptiveMetrics();
            if(HandleNetworkRecovery())
            {
                return;
            }
            UpdateAdaptivePolicy();
        }
        if(m_waiting_for_initial_presentation)
        {
            m_decoder.UpdatePresentation(0.0);
            if(m_decoder.LastPresentedTime < 0.0)
            {
                return;
            }
            if(m_decoder.HasAudio)
            {
                OpenVolumetricDecoder.AudioBufferInfo audio =
                    m_decoder.CurrentAudioBufferInfo;
                if(!audio.HasPreroll(m_audio_preroll_duration))
                {
                    return;
                }
            }

            m_waiting_for_initial_presentation = false;
            m_playback_position = 0.0;
            m_playback_state = PlaybackState.INITIALISED;
            if(m_play_after_initial_presentation)
            {
                SetScheduledStart(
                    AudioSettings.dspTime + 0.1);
            }
            Debug.Log(
                "OpenVolumetric - initial synchronized presentation ready");
            return;
        }
        if(m_decoder_recovering)
        {
            m_decoder.UpdatePresentation(m_decoder_recovery_target);
            double presented = m_decoder.LastPresentedTime;
            double tolerance = m_decoder.FrameRate > 0.0
                ? 1.0 / m_decoder.FrameRate
                : 0.034;
            // Native seek clears LastPresentedTime to -1 synchronously. The
            // first valid presentation can be later than zero when stream PTS
            // has an initial offset, so any non-negative timestamp proves the
            // new playback generation has reached the render thread.
            bool presentationRecovered = m_decoder_recovery_target <= tolerance
                ? presented >= 0.0
                : presented >= m_decoder_recovery_target - tolerance;
            if(presentationRecovered)
            {
                if(m_network_decoder_recovery)
                {
                    // One recovered presentation proves the seek succeeded,
                    // but does not mean the compressed read-ahead and decoded
                    // queues have refilled. Hold the shared clock briefly so
                    // playback does not immediately enter a lag-recovery loop.
                    if(m_network_settle_until < 0.0)
                    {
                        m_network_settle_until =
                            AudioSettings.dspTime + 1.0;
                        return;
                    }
                    if(AudioSettings.dspTime < m_network_settle_until)
                    {
                        return;
                    }
                    m_network_decoder_recovery = false;
                    m_network_settle_until = -1.0;
                    m_recovery_policy.SuppressAfterNetworkRecovery(
                        AudioSettings.dspTime);
                }
                m_decoder_recovering = false;
                m_playback_position = m_decoder_recovery_target;
                Play();
                Debug.Log("OpenVolumetric - synchronized recovery complete");
            }
            return;
        }
        if(m_waiting_for_audio_preroll)
        {
            // A seek invalidates queued PCM. Keep the requested visual frame
            // warm while the native worker reconstructs the audio window.
            // Do not submit Duration to UpdatePresentation for non-looping
            // playback: its modulo timeline would turn Duration into zero and
            // begin a native loop before the managed stop state is applied.
            if(!enableLoop && m_playback_position >= Duration)
            {
                StopAtEnd();
                return;
            }

            m_decoder.UpdatePresentation(m_playback_position);
            if(!HasRequiredAudioPreroll())
            {
                return;
            }

            m_waiting_for_audio_preroll = false;
            SchedulePlayback(
                AudioSettings.dspTime + 0.1);
            Debug.Log("OpenVolumetric - synchronized audio preroll ready");
            return;
        }
        if (m_playback_state == PlaybackState.SCHEDULED &&
            AudioSettings.dspTime >= m_clock.ScheduledDspTime)
        {
            m_playback_state = PlaybackState.PLAYING;
            m_clock.BeginScheduledPlayback();
        }
        if (m_playback_state == PlaybackState.PLAYING)
        {
            double dspTime = AudioSettings.dspTime;
            // Accumulate the shared visual clock from the same Unity DSP
            // timeline used by the native audio effect.
            double delta;
            OpenVolumetricPlaybackClock.AdvanceResult advance =
                m_clock.Advance(dspTime, out delta);
            if(advance == OpenVolumetricPlaybackClock.AdvanceResult.Discontinuity &&
                delta > 0.5)
            {
                // Desktop minimization and mobile lifecycle transitions can
                // suspend Update while Unity's audio graph keeps running.
                // Resuming from the stale visual time would permanently leave
                // texture/geometry behind audio. AudioSource.time identifies
                // the media position Unity reached; a unified seek flushes all
                // native queues and restarts them as one generation.
                double recoveryTime = m_audio_source != null &&
                    m_audio_source.clip != null
                    ? m_audio_source.time
                    : m_playback_position;
                Debug.LogWarning(string.Format(
                    "OpenVolumetric - playback clock advanced by {0:F3}s " +
                    "while rendering was suspended; resynchronizing at {1:F3}s",
                    delta,
                    recoveryTime));
                Seek(recoveryTime);
                return;
            }
            // Stop before UpdatePresentation receives Duration. The decoder
            // uses a modulo media timeline, so submitting Duration would
            // request frame zero even though managed looping is disabled.
            if(!enableLoop && m_playback_position >= Duration)
            {
                StopAtEnd();
                return;
            }

            m_decoder.UpdatePresentation(m_playback_position);
            if(enableLoop && m_decoder.ContentLooped)
            {
                m_decoder.ResetLoopFlag();
                m_playback_position = 0.0;
                Play();
                return;
            }
            if(TryRecoverDecoderLag(dspTime))
            {
                return;
            }

         }

    }

    /// <summary>
    /// Uses sustained transport measurements to request a coupled quality
    /// change. The native coordinator performs the actual audio-clock commit.
    /// </summary>
    private void UpdateAdaptivePolicy()
    {
        if(!enableLiveAdaptiveSwitching || !useAdaptiveManifest ||
            adaptiveQuality != AdaptiveQuality.Auto ||
            string.IsNullOrWhiteSpace(videoUrl) ||
            m_adaptive_representations.Count < 2 ||
            m_adaptive_segment_duration <= 0.0 ||
            m_decoder == null)
        {
            return;
        }

        m_decoder.UpdateAdaptivePolicy(
            Time.realtimeSinceStartupAsDouble,
            CurrentTime,
            Duration,
            m_adaptive_segment_duration);
        m_adaptive_smoothed_throughput_bps =
            m_decoder.AdaptivePolicyThroughputBitsPerSecond;
        OpenVolumetricDecoder.AdaptiveSwitchInfo switchInfo =
            m_decoder.CurrentAdaptiveSwitchInfo;
        m_selected_representation_id = switchInfo.ActiveRepresentation;
    }

    /// <summary>Requests Low or High at the next safe boundary for testing.</summary>
    public bool RequestAdaptiveHigh(bool high)
    {
        if(!useAdaptiveManifest || m_adaptive_representations.Count < 2 ||
            string.IsNullOrWhiteSpace(videoUrl))
        {
            return false;
        }
        ulong targetIndex = high
            ? (ulong)(m_adaptive_representations.Count - 1)
            : 0;
        return m_decoder.RequestAdaptivePolicyIndex(
            targetIndex,
            Time.realtimeSinceStartupAsDouble,
            CurrentTime,
            Duration,
            m_adaptive_segment_duration) >= 0;
    }

    /// <summary>
    /// Stops a non-looping presentation without allowing the decoder's modulo
    /// timeline to begin another playback generation.
    /// </summary>
    private void StopAtEnd()
    {
        m_playback_state = PlaybackState.STOPPED;
        m_playback_position = Duration;
        m_clock.ResetTick();
        m_waiting_for_audio_preroll = false;
        m_decoder_recovering = false;
        m_network_decoder_recovery = false;
        m_network_settle_until = -1.0;
        m_decoder.ResetLoopFlag();
        m_decoder.MeshRenderer.enabled = false;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        m_decoder.StopDspAudio();
    }

    /// <summary>
    /// Freezes the synchronized timeline while the HTTP source retries and
    /// restarts from the last complete presentation after connectivity returns.
    /// </summary>
    private bool HandleNetworkRecovery()
    {
        OpenVolumetricDecoder.BufferInfo buffer = m_decoder.InputBufferInfo;
        if(!buffer.IsRemote)
        {
            return false;
        }

        if(buffer.State == OpenVolumetricDecoder.BufferState.Rebuffering)
        {
            if(!m_network_rebuffering)
            {
                m_network_rebuffering = true;
                m_resume_after_network_recovery =
                    m_playback_state == PlaybackState.PLAYING ||
                    m_playback_state == PlaybackState.SCHEDULED;
                double presented = m_decoder.LastPresentedTime;
                m_network_recovery_target = presented >= 0.0
                    ? presented
                    : m_playback_position;
                m_playback_position = m_network_recovery_target;
                m_playback_state = PlaybackState.REBUFFERING;
                m_decoder_recovering = false;
                m_network_decoder_recovery = false;
                m_network_settle_until = -1.0;
                m_clock.ResetTick();
                if(m_audio_source != null)
                {
                    m_audio_source.Stop();
                }
                m_decoder.StopDspAudio();
                Debug.LogWarning(
                    "OpenVolumetric - network interrupted; rebuffering");
            }
            return true;
        }

        if(m_network_rebuffering)
        {
            if(buffer.State == OpenVolumetricDecoder.BufferState.Error ||
                buffer.State == OpenVolumetricDecoder.BufferState.Cancelled)
            {
                m_network_rebuffering = false;
                m_playback_state = PlaybackState.INIT_FAIL;
                Debug.LogError(
                    "OpenVolumetric - network recovery retries were exhausted");
                return true;
            }
            if(buffer.State != OpenVolumetricDecoder.BufferState.Ready)
            {
                return true;
            }

            m_network_rebuffering = false;
            if(!m_decoder.Seek(m_network_recovery_target))
            {
                m_playback_state = PlaybackState.INIT_FAIL;
                Debug.LogError(
                    "OpenVolumetric - failed to resynchronize after network recovery");
                return true;
            }
            m_playback_position = m_network_recovery_target;
            m_decoder.UpdatePresentation(m_network_recovery_target);
            if(m_resume_after_network_recovery)
            {
                m_decoder_recovering = true;
                m_decoder_recovery_target = m_network_recovery_target;
                m_network_decoder_recovery = true;
                m_network_settle_until = -1.0;
                m_playback_state = PlaybackState.PREROLLING;
                Debug.Log(
                    "OpenVolumetric - network transport restored; " +
                    "refilling decoder queues");
            }
            else
            {
                m_playback_state = PlaybackState.PAUSED;
                Debug.Log("OpenVolumetric - network recovery complete");
            }
            return true;
        }

        if(buffer.State == OpenVolumetricDecoder.BufferState.Error)
        {
            m_playback_state = PlaybackState.INIT_FAIL;
            if(m_audio_source != null)
            {
                m_audio_source.Stop();
            }
            return true;
        }
        return false;
    }

    /// <summary>
    /// Pauses all streams when native presentation remains behind the engine
    /// clock, allowing texture and geometry to catch up without losing sync.
    /// </summary>
    /// <returns>True when synchronized recovery was started.</returns>
    private bool TryRecoverDecoderLag(double dspTime)
    {
        double presented = m_decoder.LastPresentedTime;
        double target;
        double lag;
        if(!m_recovery_policy.ShouldRecover(
            dspTime,
            m_playback_position,
            presented,
            Duration,
            m_decoder.FrameRate,
            enableLoop,
            out target,
            out lag))
        {
            return false;
        }
        Debug.LogWarning(string.Format(
            "OpenVolumetric - decoder lagged by {0:F3}s; " +
            "pausing all streams to catch up at {1:F3}s",
            lag,
            target));
        m_decoder_recovering = true;
        m_decoder_recovery_target = target;
        m_playback_position = target;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        m_decoder.StopDspAudio();
        return true;
    }

    /// <summary>Stops native workers before Unity tears down the application.</summary>
    void OnApplicationQuit() 
    {
        Shutdown();
	}

    /// <summary>Releases native and Unity resources when the component dies.</summary>
    void OnDestroy() 
    {
        Shutdown();
    }

    /// <summary>
    /// Idempotently stops audio/decoding and disposes the native instance.
    /// </summary>
    private void Shutdown()
    {
        CloseAdaptiveMetrics();
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        if(m_decoder != null)
        {
            m_decoder.StopDspAudio();
            if(m_decoder.DecoderStatus ==
                OpenVolumetricDecoder.DecoderState.STARTED)
            {
                m_decoder.StopDecoding();
            }
            m_decoder.Dispose();
            m_decoder = null;
        }
        m_playback_state = PlaybackState.STOPPED;
    }

    /// <summary>
    /// Samples engine and native adaptive diagnostics into a stable CSV schema.
    /// Transition edges derive rebuffer durations and preparation latency.
    /// </summary>
    private void RecordAdaptiveMetrics()
    {
        if(!recordAdaptiveMetrics || !useAdaptiveManifest || m_decoder == null)
        {
            return;
        }
        m_adaptive_metrics.Record(
            adaptiveMetricsFileName,
            adaptiveMetricsInterval,
            new OpenVolumetricAdaptiveMetrics.Sample
        {
            WallTime = Time.realtimeSinceStartupAsDouble,
            MediaTime = CurrentTime,
            PlaybackState = m_playback_state.ToString(),
            Buffer = m_decoder.InputBufferInfo,
            Switching = m_decoder.CurrentAdaptiveSwitchInfo,
            SmoothedThroughputBitsPerSecond =
                m_adaptive_smoothed_throughput_bps,
            PresentedTime = m_decoder.LastPresentedTime,
            FrameMilliseconds = Time.unscaledDeltaTime * 1000.0F,
            Error = m_decoder.LastError
        });
    }

    /// <summary>Flushes and closes the optional adaptive metrics output.</summary>
    private void CloseAdaptiveMetrics()
    {
        m_adaptive_metrics.Dispose();
    }

    /// <summary>
    /// Schedules synchronized playback at an absolute future DSP timestamp.
    /// </summary>
    public void SetScheduledStart(double dspTime)
    {
        m_playback_position = 0.0;
        SchedulePlayback(dspTime);
    }

    /// <summary>
    /// Arms audio and visuals against one future Unity DSP timestamp.
    /// </summary>
    private void SchedulePlayback(double dspTime)
    {
        m_clock.Schedule(dspTime, AudioSettings.dspTime);
        m_playback_state = PlaybackState.SCHEDULED;
        ScheduleAudio();
    }

    /// <summary>Whether decoded PCM can fill Unity's complete DSP queue.</summary>
    private bool HasRequiredAudioPreroll()
    {
        return m_decoder == null ||
            !m_decoder.HasAudio ||
            m_decoder.CurrentAudioBufferInfo.HasPreroll(
                m_audio_preroll_duration);
    }

    /// <summary>
    /// Schedules native DSP audio on the shared visual timeline.
    /// </summary>
    private void ScheduleAudio()
    {
        if(m_audio_source != null && m_audio_source.clip != null)
        {
            m_audio_source.time = (float)m_playback_position;
            if(!m_decoder.ScheduleDspAudio(
                m_clock.ScheduledDspTime,
                m_playback_position))
            {
                Debug.LogError(
                    "OpenVolumetric - failed to schedule native DSP audio");
                m_playback_state = PlaybackState.INIT_FAIL;
                return;
            }
            m_audio_source.PlayScheduled(m_clock.ScheduledDspTime);
        }
    }

    /// <summary>Toggles between paused and scheduled/playing states.</summary>
    public void TogglePlayPause()
    {
        if(m_playback_state == PlaybackState.PLAYING ||
            m_playback_state == PlaybackState.SCHEDULED)
        {
            PausePlayback();
        }
        else
        {
            Play();
        }
    }

    /// <summary>
    /// Resumes from the current position using a short scheduling lead time.
    /// </summary>
    public void Play()
    {
        if(m_decoder == null ||
            m_playback_state == PlaybackState.INIT_FAIL ||
            m_playback_state == PlaybackState.UNINITIALISED)
        {
            return;
        }

        // STOPPED is the authoritative end-of-content state. Do not rely only
        // on an exact floating-point comparison with Duration: the final media
        // PTS can be a fraction of a frame shorter than the container duration.
        double endTolerance = m_decoder.FrameRate > 0.0
            ? 1.0 / m_decoder.FrameRate
            : 0.034;
        bool restartFromEnd =
            m_playback_state == PlaybackState.STOPPED ||
            m_playback_position >= Duration - endTolerance;
        if(restartFromEnd)
        {
            // Restart as a new synchronized decoder generation. Wait for the
            // first texture/geometry presentation before scheduling audio;
            // otherwise audio can run while the render thread is still
            // waiting for post-seek visual data.
            if(!m_decoder.Seek(0.0))
            {
                Debug.LogError(
                    "OpenVolumetric - failed to restart playback from the beginning");
                m_playback_state = PlaybackState.INIT_FAIL;
                return;
            }
            m_playback_position = 0.0;
            m_decoder.MeshRenderer.enabled = true;
            m_clock.ResetTick();
            m_waiting_for_audio_preroll = false;
            m_decoder_recovery_target = 0.0;
            m_decoder_recovering = true;
            m_network_decoder_recovery = false;
            m_network_settle_until = -1.0;
            m_playback_state = PlaybackState.PREROLLING;
            return;
        }

        m_decoder.MeshRenderer.enabled = true;
        // Seek resets the native presented timestamp. Do not let a quickly
        // refilled audio queue start the clock until texture and geometry from
        // that same seek generation have reached Unity's render thread.
        if(m_decoder.LastPresentedTime < 0.0)
        {
            m_decoder_recovery_target = m_playback_position;
            m_decoder_recovering = true;
            m_network_decoder_recovery = false;
            m_network_settle_until = -1.0;
            m_clock.ResetTick();
            m_waiting_for_audio_preroll = false;
            m_playback_state = PlaybackState.PREROLLING;
            return;
        }
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        m_decoder.StopDspAudio();
        if(!HasRequiredAudioPreroll())
        {
            m_waiting_for_audio_preroll = true;
            m_clock.ResetTick();
            m_playback_state = PlaybackState.PREROLLING;
            return;
        }

        m_waiting_for_audio_preroll = false;
        SchedulePlayback(
            AudioSettings.dspTime + 0.1);
    }

    /// <summary>Freezes the shared timeline and stops audio consumption.</summary>
    public void PausePlayback()
    {
        if(m_playback_state != PlaybackState.PLAYING &&
            m_playback_state != PlaybackState.SCHEDULED)
        {
            return;
        }

        m_playback_position = System.Math.Min(CurrentTime, Duration);
        m_clock.ResetTick();
        m_waiting_for_audio_preroll = false;
        m_playback_state = PlaybackState.PAUSED;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
        }
        m_decoder.StopDspAudio();
    }

    /// <summary>
    /// Seeks every native stream and updates Unity audio to the clamped target.
    /// </summary>
    /// <returns>False when the decoder is unavailable or native seek fails.</returns>
    public bool Seek(double time)
    {
        if(m_decoder == null || Duration <= 0.0)
        {
            return false;
        }

        double target = System.Math.Max(0.0, System.Math.Min(time, Duration));
        bool resume = m_playback_state == PlaybackState.PLAYING ||
            m_playback_state == PlaybackState.SCHEDULED;
        if(!m_decoder.Seek(target))
        {
            return false;
        }

        m_playback_position = target;
        m_decoder.MeshRenderer.enabled = true;
        if(m_audio_source != null)
        {
            m_audio_source.Stop();
            m_audio_source.time = (float)target;
        }

        if(resume)
        {
            Play();
        }
        else
        {
            m_playback_state = PlaybackState.PAUSED;
            m_decoder.UpdatePresentation(target);
        }
        return true;
    }

    /// <summary>Seeks by a signed number of seconds from the current time.</summary>
    public void SeekRelative(double seconds)
    {
        Seek(CurrentTime + seconds);
    }

    /// <summary>Changes loop policy for both native playback and Unity audio.</summary>
    public void ToggleLoop()
    {
        enableLoop = !enableLoop;
        if(m_audio_source != null)
        {
            m_audio_source.loop = enableLoop;
        }
        if(enableLoop && m_decoder != null)
        {
            m_decoder.ResetLoopFlag();
        }
    }


    /// <summary>
    /// Applies Inspector colour-correction edits during active playback.
    /// </summary>
    private void OnValidate()
    {
        if(m_playback_state == PlaybackState.PLAYING)
        {
            m_decoder.SetColourCorrectionValues(
                luminanceCorrection,
                blueProjectionCorrection,
                redProjectionCorrection);
        }
    }

}

}
