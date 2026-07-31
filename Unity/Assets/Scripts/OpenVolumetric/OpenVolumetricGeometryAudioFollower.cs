using UnityEngine;

namespace OpenVolumetric
{

/// <summary>
/// Places the decoded spatial-audio source at the current geometry centroid.
///
/// Add this component beside OpenVolumetric before entering Play mode. The
/// player then creates its AudioSource on a child transform so audio can move
/// independently from the volumetric object's root transform.
/// </summary>
[RequireComponent(typeof(OpenVolumetric))]
public sealed class OpenVolumetricGeometryAudioFollower : MonoBehaviour
{
    [Tooltip("Spatial blend applied to the OpenVolumetric AudioSource.")]
    [Range(0.0F, 1.0F)]
    public float spatialBlend = 1.0F;

    [Tooltip("Movement smoothing in seconds. Set to zero for exact tracking.")]
    [Min(0.0F)]
    public float smoothTime = 0.05F;

    private OpenVolumetric m_player;
    private Vector3 m_velocity;

    private void Awake()
    {
        m_player = GetComponent<OpenVolumetric>();
    }

    private void LateUpdate()
    {
        AudioSource audioSource = m_player.AudioOutput;
        Vector3 centroid;
        if(audioSource == null ||
            !m_player.TryGetGeometryCentroid(out centroid))
        {
            return;
        }

        audioSource.spatialBlend = spatialBlend;
        audioSource.spatialize = true;
        audioSource.transform.localPosition = smoothTime > 0.0F
            ? Vector3.SmoothDamp(
                audioSource.transform.localPosition,
                centroid,
                ref m_velocity,
                smoothTime)
            : centroid;
    }
}

}
