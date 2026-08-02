using System;
using System.Globalization;
using System.IO;
using UnityEngine;

namespace OpenVolumetric
{

/// <summary>
/// Owns the optional adaptive-evaluation CSV and transition-derived metrics.
/// It consumes immutable snapshots and has no authority over playback.
/// </summary>
internal sealed class OpenVolumetricAdaptiveMetrics : IDisposable
{
    internal struct Sample
    {
        public double WallTime;
        public double MediaTime;
        public string PlaybackState;
        public OpenVolumetricDecoder.BufferInfo Buffer;
        public OpenVolumetricDecoder.AdaptiveSwitchInfo Switching;
        public double SmoothedThroughputBitsPerSecond;
        public double PresentedTime;
        public float FrameMilliseconds;
        public string Error;
    }

    private StreamWriter m_writer;
    private double m_started;
    private double m_nextSample;
    private double m_switchStarted = -1.0;
    private double m_lastSwitchLatency = -1.0;
    private ulong m_switchFailureCount;
    private double m_rebufferStarted = -1.0;
    private double m_totalRebufferTime;
    private ulong m_rebufferCount;
    private OpenVolumetricDecoder.AdaptiveSwitchState m_previousSwitchState =
        OpenVolumetricDecoder.AdaptiveSwitchState.Stable;
    private OpenVolumetricDecoder.BufferState m_previousBufferState =
        OpenVolumetricDecoder.BufferState.Opening;

    internal void Record(string filename, double interval, Sample sample)
    {
        EnsureOpen(filename, sample.WallTime);
        UpdateTransitions(sample);
        if(sample.WallTime < m_nextSample)
        {
            return;
        }
        m_nextSample = sample.WallTime + Math.Max(0.05, interval);
        double rebufferSeconds = m_totalRebufferTime;
        if(m_rebufferStarted >= 0.0)
        {
            rebufferSeconds += sample.WallTime - m_rebufferStarted;
        }
        m_writer.WriteLine(string.Join(",", new string[]
        {
            Number(sample.WallTime - m_started),
            Number(sample.MediaTime),
            Csv(sample.PlaybackState),
            Csv(sample.Buffer.State.ToString()),
            Csv(sample.Switching.ActiveRepresentation),
            Csv(sample.Switching.PendingRepresentation),
            Csv(sample.Switching.State.ToString()),
            Number(sample.SmoothedThroughputBitsPerSecond / 1000000.0),
            sample.Buffer.DownloadedBytes.ToString(),
            sample.Buffer.CachedBytes.ToString(),
            sample.Buffer.RequestCount.ToString(),
            sample.Buffer.RecoveryCount.ToString(),
            sample.Buffer.ActiveFragment.ToString(),
            sample.Buffer.CachedFragmentCount.ToString(),
            m_rebufferCount.ToString(),
            Number(rebufferSeconds),
            sample.Switching.SwitchCount.ToString(),
            m_switchFailureCount.ToString(),
            Number(m_lastSwitchLatency),
            Number(sample.PresentedTime),
            Number(sample.MediaTime - sample.PresentedTime),
            Number(sample.FrameMilliseconds),
            UnityEngine.Profiling.Profiler.GetTotalAllocatedMemoryLong().ToString(),
            Csv(sample.Error)
        }));
    }

    public void Dispose()
    {
        if(m_writer == null)
        {
            return;
        }
        m_writer.Flush();
        m_writer.Dispose();
        m_writer = null;
    }

    private void EnsureOpen(string filename, double now)
    {
        if(m_writer != null)
        {
            return;
        }
        string safeName = string.IsNullOrWhiteSpace(filename)
            ? "openvolumetric-adaptive-metrics.csv"
            : Path.GetFileName(filename);
        string path = Path.Combine(Application.persistentDataPath, safeName);
        m_writer = new StreamWriter(path, false) { AutoFlush = true };
        m_writer.WriteLine(
            "wall_seconds,media_seconds,playback_state,input_state," +
            "active_representation,pending_representation,switch_state," +
            "throughput_mbps,downloaded_bytes,cached_bytes,http_requests," +
            "network_recoveries,active_fragment,cached_fragments," +
            "rebuffer_count,rebuffer_seconds,switch_count," +
            "switch_failures,last_switch_latency_seconds,presented_seconds," +
            "av_error_seconds,frame_ms,engine_memory_bytes,error");
        m_started = now;
        m_nextSample = now;
        Debug.Log("OpenVolumetric - recording adaptive metrics: " + path);
    }

    private void UpdateTransitions(Sample sample)
    {
        bool rebuffering = sample.Buffer.State ==
            OpenVolumetricDecoder.BufferState.Rebuffering;
        bool wasRebuffering = m_previousBufferState ==
            OpenVolumetricDecoder.BufferState.Rebuffering;
        if(rebuffering && !wasRebuffering)
        {
            ++m_rebufferCount;
            m_rebufferStarted = sample.WallTime;
        }
        else if(!rebuffering && wasRebuffering && m_rebufferStarted >= 0.0)
        {
            m_totalRebufferTime += sample.WallTime - m_rebufferStarted;
            m_rebufferStarted = -1.0;
        }
        m_previousBufferState = sample.Buffer.State;

        if(sample.Switching.State ==
                OpenVolumetricDecoder.AdaptiveSwitchState.Preparing &&
            m_previousSwitchState != sample.Switching.State)
        {
            m_switchStarted = sample.WallTime;
        }
        if(sample.Switching.State ==
                OpenVolumetricDecoder.AdaptiveSwitchState.Failed &&
            m_previousSwitchState != sample.Switching.State)
        {
            ++m_switchFailureCount;
            FinishSwitch(sample.WallTime);
        }
        if(sample.Switching.State ==
                OpenVolumetricDecoder.AdaptiveSwitchState.Stable &&
            sample.Switching.SwitchCount > 0 &&
            m_previousSwitchState != sample.Switching.State)
        {
            FinishSwitch(sample.WallTime);
        }
        m_previousSwitchState = sample.Switching.State;
    }

    private void FinishSwitch(double now)
    {
        if(m_switchStarted < 0.0)
        {
            return;
        }
        m_lastSwitchLatency = now - m_switchStarted;
        m_switchStarted = -1.0;
    }

    private static string Number(double value)
    {
        return value.ToString("F6", CultureInfo.InvariantCulture);
    }

    private static string Csv(string value)
    {
        string safe = value ?? string.Empty;
        return "\"" + safe.Replace("\"", "\"\"") + "\"";
    }
}

}
