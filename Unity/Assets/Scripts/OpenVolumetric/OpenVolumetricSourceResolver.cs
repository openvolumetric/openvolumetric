using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;

namespace OpenVolumetric
{

/// <summary>
/// Resolves a Unity input configuration into one directly playable MP4.
///
/// This class owns path/URL validation and adaptive-manifest selection. It
/// never creates Unity scene objects or starts playback, so the component can
/// keep source preparation separate from engine-resource lifetime.
/// </summary>
internal sealed class OpenVolumetricSourceResolver
{
    private const string PluginName = "AudioPluginOpenVolumetricUnity";
    private const int MaximumAdaptiveRepresentations = 32;

    internal sealed class Representation
    {
        public string Id;
        public string Resource;
        public ulong Bandwidth;
    }

    internal sealed class Request
    {
        public string Filename;
        public string Url;
        public bool UseAdaptiveManifest;
        public OpenVolumetric.AdaptiveQuality Quality;
        public int MaximumTextureDimension;
        public float MaximumTextureBitrateMbps;
        public float MaximumGeometryBitrateMbps;
        public float MaximumBandwidthMbps;
    }

    internal sealed class Result
    {
        public string PlayableResource;
        public string RepresentationId = string.Empty;
        public ulong MeasuredThroughputBitsPerSecond;
        public string DecisionReason = string.Empty;
        public double SegmentDuration;
        public readonly List<Representation> Representations =
            new List<Representation>();
        public string Error = string.Empty;

        public bool Succeeded
        {
            get { return !string.IsNullOrEmpty(PlayableResource); }
        }
    }

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

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct NativeAdaptiveSelectionRequest
    {
        public uint StructSize;
        public string ManifestJson;
        public string ManifestLocation;
        public int Quality;
        public uint MaximumTextureWidth;
        public uint MaximumTextureHeight;
        public ulong MaximumTextureBitrate;
        public ulong MaximumGeometryBitrate;
        public ulong MaximumBandwidth;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct NativeAdaptiveRepresentation
    {
        public uint StructSize;
        public ulong Bandwidth;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Id;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string Resource;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct NativeAdaptiveSelection
    {
        public uint StructSize;
        public ulong MeasuredThroughputBitsPerSecond;
        public uint RepresentationCount;
        public double SegmentDuration;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string RepresentationId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string ResourceUri;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string ResolvedResource;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string DecisionReason;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string Error;
    }

    private struct CapabilityLimits
    {
        public uint MaximumTextureWidth;
        public uint MaximumTextureHeight;
        public ulong MaximumTextureBitrate;
        public ulong MaximumGeometryBitrate;
        public ulong MaximumBandwidth;
    }

    [DllImport(PluginName, CharSet = CharSet.Ansi)]
    private static extern NativeResult openvolumetric_adaptive_select(
        ref NativeAdaptiveSelectionRequest request,
        ref NativeAdaptiveSelection selection,
        [In, Out] NativeAdaptiveRepresentation[] representations,
        uint representationCapacity);

    /// <summary>Completes with one result for local, HTTP, or manifest input.</summary>
    internal IEnumerator Resolve(Request request, Action<Result> completed)
    {
        Result result = new Result();
        if(request.UseAdaptiveManifest)
        {
            yield return ResolveAdaptive(request, result);
        }
        else if(!string.IsNullOrWhiteSpace(request.Url))
        {
            Uri remoteUri;
            if(!TryGetHttpUri(request.Url, out remoteUri))
            {
                result.Error = "videoUrl must be an HTTP or HTTPS URL";
            }
            else
            {
                result.PlayableResource = remoteUri.AbsoluteUri;
            }
        }
        else
        {
            yield return StreamingAssetFile.PrepareReadablePath(
                request.Filename,
                path => result.PlayableResource = path);
            if(string.IsNullOrEmpty(result.PlayableResource))
            {
                result.Error = "Failed to prepare volumetric video input";
            }
        }
        completed(result);
    }

    private IEnumerator ResolveAdaptive(Request request, Result result)
    {
        string manifestJson = null;
        string manifestLocation = null;
        bool remote = !string.IsNullOrWhiteSpace(request.Url);
        if(remote)
        {
            Uri manifestUri;
            if(!TryGetHttpUri(request.Url, out manifestUri))
            {
                result.Error =
                    "Adaptive manifest URL must use HTTP or HTTPS";
                yield break;
            }
            manifestLocation = manifestUri.AbsoluteUri;
        }
        else
        {
            yield return StreamingAssetFile.PrepareReadablePath(
                request.Filename,
                path => manifestLocation = path);
            if(string.IsNullOrEmpty(manifestLocation))
            {
                result.Error = "Failed to prepare adaptive manifest input";
                yield break;
            }
            manifestJson = File.ReadAllText(manifestLocation);
        }

        NativeAdaptiveSelection selection;
        NativeAdaptiveRepresentation[] representations;
        NativeResult nativeResult = SelectAdaptive(
            request,
            manifestJson,
            manifestLocation,
            GetCapabilityLimits(request),
            out selection,
            out representations);
        if(nativeResult != NativeResult.Ok)
        {
            result.Error = "Adaptive manifest selection failed (" +
                nativeResult + "): " + (selection.Error ?? string.Empty);
            yield break;
        }

        result.RepresentationId = selection.RepresentationId ?? string.Empty;
        result.MeasuredThroughputBitsPerSecond =
            selection.MeasuredThroughputBitsPerSecond;
        result.DecisionReason = selection.DecisionReason ?? string.Empty;
        result.SegmentDuration = selection.SegmentDuration;
        uint count = Math.Min(
            selection.RepresentationCount,
            (uint)representations.Length);
        for(uint index = 0; index < count; ++index)
        {
            result.Representations.Add(new Representation
            {
                Id = representations[index].Id ?? string.Empty,
                Resource = representations[index].Resource ?? string.Empty,
                Bandwidth = representations[index].Bandwidth
            });
        }

        if(remote)
        {
            result.PlayableResource = selection.ResolvedResource;
            yield break;
        }

        string resourceUri = selection.ResourceUri ?? string.Empty;
        string manifestDirectory = Path.GetDirectoryName(request.Filename) ??
            string.Empty;
        yield return StreamingAssetFile.PrepareReadablePath(
            Path.Combine(manifestDirectory, resourceUri),
            path => result.PlayableResource = path);
        if(string.IsNullOrEmpty(result.PlayableResource))
        {
            result.Error = "Failed to prepare selected adaptive representation";
        }
    }

    private static NativeResult SelectAdaptive(
        Request source,
        string manifestJson,
        string manifestLocation,
        CapabilityLimits limits,
        out NativeAdaptiveSelection selection,
        out NativeAdaptiveRepresentation[] representations)
    {
        NativeAdaptiveSelectionRequest request =
            new NativeAdaptiveSelectionRequest
            {
                StructSize =
                    (uint)Marshal.SizeOf<NativeAdaptiveSelectionRequest>(),
                ManifestJson = manifestJson,
                ManifestLocation = manifestLocation,
                Quality = (int)source.Quality,
                MaximumTextureWidth = limits.MaximumTextureWidth,
                MaximumTextureHeight = limits.MaximumTextureHeight,
                MaximumTextureBitrate = limits.MaximumTextureBitrate,
                MaximumGeometryBitrate = limits.MaximumGeometryBitrate,
                MaximumBandwidth = limits.MaximumBandwidth
            };
        selection = new NativeAdaptiveSelection
        {
            StructSize = (uint)Marshal.SizeOf<NativeAdaptiveSelection>()
        };
        representations =
            new NativeAdaptiveRepresentation[MaximumAdaptiveRepresentations];
        uint representationSize =
            (uint)Marshal.SizeOf<NativeAdaptiveRepresentation>();
        for(int index = 0; index < representations.Length; ++index)
        {
            representations[index].StructSize = representationSize;
        }
        return openvolumetric_adaptive_select(
            ref request,
            ref selection,
            representations,
            (uint)representations.Length);
    }

    private static CapabilityLimits GetCapabilityLimits(Request request)
    {
        bool android = Application.platform == RuntimePlatform.Android;
        uint platformDimension = android ? 4096U : 8192U;
        uint reportedDimension = (uint)Math.Max(1, SystemInfo.maxTextureSize);
        uint maximumDimension = request.MaximumTextureDimension > 0
            ? (uint)request.MaximumTextureDimension
            : Math.Min(platformDimension, reportedDimension);
        return new CapabilityLimits
        {
            MaximumTextureWidth = maximumDimension,
            MaximumTextureHeight = maximumDimension,
            MaximumTextureBitrate = ResolveBitrateLimit(
                request.MaximumTextureBitrateMbps,
                android ? 20000000UL : 100000000UL),
            MaximumGeometryBitrate = ResolveBitrateLimit(
                request.MaximumGeometryBitrateMbps,
                android ? 50000000UL : 250000000UL),
            MaximumBandwidth = ResolveBitrateLimit(
                request.MaximumBandwidthMbps,
                android ? 70000000UL : 350000000UL)
        };
    }

    private static ulong ResolveBitrateLimit(
        float overrideMbps,
        ulong platformLimit)
    {
        return overrideMbps > 0.0F
            ? (ulong)Math.Round(overrideMbps * 1000000.0)
            : platformLimit;
    }

    private static bool TryGetHttpUri(string value, out Uri uri)
    {
        return Uri.TryCreate(value.Trim(), UriKind.Absolute, out uri) &&
            (uri.Scheme == Uri.UriSchemeHttp ||
             uri.Scheme == Uri.UriSchemeHttps);
    }
}

}
