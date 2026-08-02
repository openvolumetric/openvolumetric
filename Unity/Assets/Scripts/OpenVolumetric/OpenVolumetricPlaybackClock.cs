using System;

namespace OpenVolumetric
{

/// <summary>
/// Owns Unity DSP-to-media clock mapping without touching Unity scene objects.
/// Playback commands remain on the component's main thread; this coordinator
/// only calculates scheduling and discontinuity results.
/// </summary>
internal sealed class OpenVolumetricPlaybackClock
{
    internal enum AdvanceResult
    {
        Waiting,
        Advanced,
        Discontinuity
    }

    internal double Position { get; set; }
    internal double ScheduledDspTime { get; private set; }
    internal bool HasScheduledStart { get; private set; }

    private double m_lastDspTime;
    private bool m_hasLastDspTime;

    internal double CurrentTime(bool loop, double duration)
    {
        double time = Math.Max(0.0, Position);
        return loop && duration > 0.0 ? time % duration : time;
    }

    internal double Schedule(double requestedDspTime, double currentDspTime)
    {
        ScheduledDspTime = Math.Max(currentDspTime + 0.05, requestedDspTime);
        m_lastDspTime = ScheduledDspTime;
        m_hasLastDspTime = true;
        HasScheduledStart = true;
        return ScheduledDspTime;
    }

    internal void BeginScheduledPlayback()
    {
        m_lastDspTime = ScheduledDspTime;
        m_hasLastDspTime = true;
    }

    internal AdvanceResult Advance(double dspTime, out double elapsed)
    {
        elapsed = 0.0;
        if(!m_hasLastDspTime)
        {
            m_lastDspTime = dspTime;
            m_hasLastDspTime = true;
            return AdvanceResult.Waiting;
        }
        elapsed = dspTime - m_lastDspTime;
        m_lastDspTime = dspTime;
        if(elapsed < 0.0 || elapsed > 0.5)
        {
            return AdvanceResult.Discontinuity;
        }
        Position += elapsed;
        return AdvanceResult.Advanced;
    }

    internal void ResetTick()
    {
        m_hasLastDspTime = false;
    }
}

}
