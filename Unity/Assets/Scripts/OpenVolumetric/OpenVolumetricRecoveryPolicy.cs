using System;

namespace OpenVolumetric
{

/// <summary>
/// Evaluates visual lag and recovery cooldowns without controlling Unity audio
/// or decoder objects. The component applies the returned recovery decision on
/// the main thread so engine lifetime rules remain explicit.
/// </summary>
internal sealed class OpenVolumetricRecoveryPolicy
{
    private double m_lagStarted = -1.0;
    private double m_lastRecovery = -10.0;

    /// <summary>Defers lag recovery while transport queues refill.</summary>
    internal void SuppressAfterNetworkRecovery(double dspTime)
    {
        // The normal 0.5-second cooldown combines with this 1.5-second offset
        // to preserve the existing two-second post-network settling window.
        m_lastRecovery = dspTime + 1.5;
    }

    internal bool ShouldRecover(
        double dspTime,
        double playbackPosition,
        double presentedTime,
        double duration,
        double frameRate,
        bool loop,
        out double target,
        out double lag)
    {
        target = duration > 0.0
            ? playbackPosition % duration
            : playbackPosition;
        lag = target - presentedTime;
        if(presentedTime < 0.0)
        {
            return false;
        }
        double frameDuration = frameRate > 0.0
            ? 1.0 / frameRate
            : 1.0 / 30.0;
        if(loop && duration > 0.0 &&
            duration - target <= 3.0 * frameDuration)
        {
            ResetLagWindow();
            return false;
        }
        if(lag < 0.0)
        {
            ResetLagWindow();
            return false;
        }
        double tolerance = frameRate > 0.0
            ? Math.Max(2.0 / frameRate, 0.075)
            : 0.075;
        if(lag <= tolerance)
        {
            ResetLagWindow();
            return false;
        }
        if(m_lagStarted < 0.0)
        {
            m_lagStarted = dspTime;
            return false;
        }
        if(dspTime - m_lagStarted < 0.1 ||
            dspTime - m_lastRecovery < 0.5)
        {
            return false;
        }
        m_lastRecovery = dspTime;
        ResetLagWindow();
        return true;
    }

    private void ResetLagWindow()
    {
        m_lagStarted = -1.0;
    }
}

}
