using System;
using System.Collections;
using System.IO;
using UnityEngine;
using UnityEngine.Networking;

namespace OpenVolumetric
{

/// <summary>
/// Resolves a StreamingAssets entry to a path that native libraries can open.
///
/// Android packages StreamingAssets inside the APK, so FFmpeg cannot open the
/// URI directly. The asset is copied into Unity's persistent data directory
/// before the native decoder is initialized. Desktop platforms keep using the
/// original file path.
/// </summary>
public static class StreamingAssetFile
{
    /// <summary>
    /// Produces a filesystem path that native FFmpeg can open.
    /// </summary>
    /// <remarks>
    /// Desktop StreamingAssets are already files. Android assets live inside
    /// the APK and are copied once to persistent storage before completion.
    /// </remarks>
    public static IEnumerator PrepareReadablePath(
        string relativePath,
        Action<string> completed)
    {
        if(string.IsNullOrWhiteSpace(relativePath))
        {
            Debug.LogError("StreamingAssetFile - input filename is empty");
            completed(null);
            yield break;
        }

        string source = Path.Combine(
            Application.streamingAssetsPath,
            relativePath);

#if UNITY_ANDROID && !UNITY_EDITOR
        string cacheDirectory = Path.Combine(
            Application.persistentDataPath,
            "OpenVolumetric");
        string destination = Path.Combine(
            cacheDirectory,
            Path.GetFileName(relativePath));

        using(UnityWebRequest request = UnityWebRequest.Get(source))
        {
            yield return request.SendWebRequest();
            if(request.result != UnityWebRequest.Result.Success)
            {
                Debug.LogError(
                    "StreamingAssetFile - failed to read " + source +
                    ": " + request.error);
                completed(null);
                yield break;
            }

            try
            {
                Directory.CreateDirectory(cacheDirectory);
                File.WriteAllBytes(destination, request.downloadHandler.data);
            }
            catch(Exception exception)
            {
                Debug.LogError(
                    "StreamingAssetFile - failed to cache input: " +
                    exception.Message);
                completed(null);
                yield break;
            }
        }

        completed(destination);
#else
        if(!File.Exists(source))
        {
            Debug.LogError(
                "StreamingAssetFile - input does not exist: " + source);
            completed(null);
            yield break;
        }
        completed(source);
#endif
    }
}

}
