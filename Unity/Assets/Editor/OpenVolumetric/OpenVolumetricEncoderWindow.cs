using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace OpenVolumetric.Editor
{

/// <summary>
/// One-click authoring front end for the native volumetric-video tools.
/// It converts numbered OBJ meshes to Draco, encodes the matching image
/// sequence and optional audio with FFmpeg, then calls the native authoring
/// library to add the timed geometry track and verify the resulting MP4.
/// </summary>
public sealed class OpenVolumetricEncoderWindow : EditorWindow
{
    private const string PreferencePrefix = "OpenVolumetric.Encoder.";
    private const string AuthoringLibrary = "OpenVolumetricAuthoring";

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_pack",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int PackVolumetricVideo(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string geometryDirectory,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceGeometryDirectory,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
        int positionQuantization,
        int normalQuantization,
        int textureQuantization,
        int encodeSpeed,
        int decodeSpeed,
        int enableTopologyCompression,
        int maximumGeometryKeyframeInterval,
        int fragmentDurationSeconds,
        int fragmentFrameInterval);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_encode_obj",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int EncodeObjToDraco(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string inputPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
        int positionQuantization,
        int normalQuantization,
        int textureQuantization,
        int encodeSpeed,
        int decodeSpeed,
        int enableTopologyCompression);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_last_error",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr GetAuthoringError();

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_last_report",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr GetAuthoringReport();

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeEncodingSettings
    {
        public int Codec;
        public int Crf;
        public int VideoKeyframeInterval;
        public int ReferenceFrames;
        public int DisableSao;
        public int PositionQuantization;
        public int NormalQuantization;
        public int TextureQuantization;
        public int DracoEncodeSpeed;
        public int DracoDecodeSpeed;
        public int MaximumVideoBitrateKbps;
        public int VideoBufferSizeKbps;
        public int GeometryKeyframeInterval;
        public int FragmentDurationSeconds;
    }

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_get_preset",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetNativePreset(
        int preset,
        ref NativeEncodingSettings settings);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_get_adaptive_preset",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetNativeAdaptivePreset(
        int preset,
        int quality,
        int fragmentDurationSeconds,
        ref NativeEncodingSettings settings);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_last_geometry_payload_bytes",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong GetLastGeometryPayloadBytes();

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_write_adaptive_manifest",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int WriteAdaptiveManifest(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string presentationId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string manifestPath,
        double segmentDurationSeconds,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string lowId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string lowResourcePath,
        int lowPositionQuantizationBits,
        ulong lowGeometryPayloadBytes,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string highId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string highResourcePath,
        int highPositionQuantizationBits,
        ulong highGeometryPayloadBytes,
        int temporalCompression);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_validate_sources",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int ValidateNativeSources(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string imageDirectory,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string geometryDirectory);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_build_ffmpeg_arguments",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr BuildNativeFFmpegArguments(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string imagePattern,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string audioPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath,
        double frameRate,
        int firstFrame,
        int frameCount,
        ref NativeEncodingSettings settings);

    private static readonly Regex NumberedFile =
        new Regex(@"^(?<frame>[0-9]+)$", RegexOptions.Compiled);

    private enum EncodingPreset
    {
        DesktopLocal,
        DesktopStreaming,
        QuestLocal,
        QuestStreaming,
        Custom
    }

    private enum VideoCodec
    {
        HEVC,
        H264
    }

    private enum FragmentDuration
    {
        OneSecond = 1,
        TwoSeconds = 2,
        FourSeconds = 4
    }

    [SerializeField] private EncodingPreset encodingPreset =
        EncodingPreset.QuestLocal;
    [SerializeField] private string imageDirectory = "";
    [SerializeField] private string geometryDirectory = "";
    [SerializeField] private string audioFile = "";
    [SerializeField] private string outputFile = "";
    [SerializeField] private string adaptiveOutputFolder = "";
    [SerializeField] private string presentationName = "openvolumetric";
    [SerializeField] private float frameRate = 30.0f;
    [SerializeField] private VideoCodec videoCodec = VideoCodec.HEVC;
    [SerializeField] private int crf = 25;
    [SerializeField] private int keyframeInterval = 30;
    [SerializeField] private int referenceFrames = 1;
    [SerializeField] private bool disableSao = true;
    [SerializeField] private int positionQuantization = 14;
    [SerializeField] private int normalQuantization = 10;
    [SerializeField] private int textureQuantization = 12;
    [SerializeField] private int dracoEncodeSpeed = 7;
    [SerializeField] private int dracoDecodeSpeed = 9;
    [SerializeField] private bool overwriteOutput;
    [SerializeField] private bool geometryCompression = true;
    [SerializeField] private bool limitGeometryKeyframeInterval;
    [SerializeField] private int maximumGeometryKeyframeInterval = 60;
    [SerializeField] private bool fragmentedMp4;
    [SerializeField] private bool adaptivePackage;
    [SerializeField] private FragmentDuration fragmentDuration =
        FragmentDuration.TwoSeconds;
    [SerializeField] private bool showAdvanced;
    [SerializeField] private string ffmpegPath = "";

    private CancellationTokenSource cancellation;
    private Vector2 scroll;
    private readonly StringBuilder log = new StringBuilder();
    private volatile float progress;
    private volatile bool running;
    private volatile string status = "Ready";

    /// <summary>Opens or focuses the OpenVolumetric authoring window.</summary>
    [MenuItem("Tools/OpenVolumetric/Encoder")]
    private static void Open()
    {
        OpenVolumetricEncoderWindow window =
            GetWindow<OpenVolumetricEncoderWindow>("OpenVolumetric Encoder");
        window.minSize = new Vector2(560, 620);
    }

    /// <summary>Restores persisted paths when Unity creates the window.</summary>
    private void OnEnable()
    {
        imageDirectory = Load("Images", imageDirectory);
        geometryDirectory = Load("Geometry", geometryDirectory);
        audioFile = Load("Audio", audioFile);
        outputFile = Load("Output", DefaultOutputPath());
        adaptiveOutputFolder = Load(
            "AdaptiveOutputFolder", DefaultAdaptiveOutputFolder());
        presentationName = Load("PresentationName", "openvolumetric");
        ffmpegPath = Load("FFmpeg", FindExecutable("ffmpeg"));
        geometryCompression = EditorPrefs.GetBool(
            PreferencePrefix + "GeometryCompression", true);
        limitGeometryKeyframeInterval = EditorPrefs.GetBool(
            PreferencePrefix + "LimitGeometryKeyframeInterval", false);
        maximumGeometryKeyframeInterval = EditorPrefs.GetInt(
            PreferencePrefix + "MaximumGeometryKeyframeInterval", 60);
        fragmentedMp4 = EditorPrefs.GetBool(
            PreferencePrefix + "FragmentedMp4", false);
        adaptivePackage = EditorPrefs.GetBool(
            PreferencePrefix + "AdaptivePackage", false);
        fragmentDuration = (FragmentDuration)EditorPrefs.GetInt(
            PreferencePrefix + "FragmentDuration", 2);
    }

    /// <summary>Persists current paths before the window is destroyed.</summary>
    private void OnDisable()
    {
        SaveSettings();
    }

    /// <summary>Draws input, preset, progress, and diagnostic controls.</summary>
    private void OnGUI()
    {
        scroll = EditorGUILayout.BeginScrollView(scroll);
        EditorGUILayout.LabelField("OpenVolumetric Encoder", EditorStyles.boldLabel);
        EditorGUILayout.HelpBox(
            "Creates one MP4 from matching numbered images and OBJ meshes. " +
            "OBJ files are Draco-encoded by the native authoring library; " +
            "no separate Draco tool is required.",
            MessageType.Info);

        using (new EditorGUI.DisabledScope(running))
        {
            imageDirectory = DirectoryField("Image Sequence", imageDirectory);
            geometryDirectory = DirectoryField("OBJ Sequence", geometryDirectory);
            audioFile = OptionalFileField(
                "Audio (optional)",
                audioFile,
                "Select audio",
                "");
            if(adaptivePackage)
            {
                adaptiveOutputFolder = DirectoryField(
                    "Output Parent Folder", adaptiveOutputFolder);
                presentationName = EditorGUILayout.TextField(
                    new GUIContent(
                        "Presentation Name",
                        "Creates a package folder with this name containing manifest.json, low.mp4, and high.mp4."),
                    presentationName);
                EditorGUILayout.HelpBox(
                    "Package: " + AdaptivePackageDirectory(),
                    MessageType.None);
            }
            else
            {
                outputFile = SaveFileField("Output MP4", outputFile);
            }

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Encoding Settings", EditorStyles.boldLabel);
            encodingPreset = (EncodingPreset)EditorGUILayout.EnumPopup(
                new GUIContent(
                    "Platform Preset",
                    "Selects video and geometry settings for the target platform."),
                encodingPreset);
            frameRate = EditorGUILayout.FloatField(
                new GUIContent(
                    "Source Frame Rate",
                    "Controls image-sequence encoding. Geometry timing is read back from the encoded video samples."),
                frameRate);
            adaptivePackage = EditorGUILayout.Toggle(
                new GUIContent(
                    "Adaptive Package",
                    "Encode aligned low/high representations and an adaptive JSON manifest. Requires a streaming preset."),
                adaptivePackage);
            if(adaptivePackage)
            {
                fragmentedMp4 = true;
            }
            using (new EditorGUI.DisabledScope(adaptivePackage))
            {
            fragmentedMp4 = EditorGUILayout.Toggle(
                new GUIContent(
                    "Fragmented MP4",
                    "Write an initialization section followed by independently addressable MP4 fragments with aligned video and geometry keyframes."),
                fragmentedMp4);
            }
            using (new EditorGUI.DisabledScope(!fragmentedMp4))
            {
                fragmentDuration = (FragmentDuration)EditorGUILayout.EnumPopup(
                    new GUIContent(
                        "Fragment Duration",
                        "Select 1, 2, or 4 second aligned fragments."),
                    fragmentDuration);
            }
            geometryCompression = EditorGUILayout.Toggle(
                new GUIContent(
                    "Geometry Compression",
                    "Reuse matching mesh topology and encode dependent frames as position-only Draco point clouds. Disable to encode every packet as an independent Draco mesh."),
                geometryCompression);
            using (new EditorGUI.DisabledScope(!geometryCompression))
            {
                limitGeometryKeyframeInterval = EditorGUILayout.Toggle(
                    new GUIContent(
                        "Limit Geometry Keyframes",
                        "Force a full Draco reference mesh periodically to bound geometry seek and streaming preroll."),
                    limitGeometryKeyframeInterval);
                using (new EditorGUI.DisabledScope(
                    !limitGeometryKeyframeInterval))
                {
                    maximumGeometryKeyframeInterval =
                        EditorGUILayout.IntField(
                            new GUIContent(
                                "Maximum Geometry Frames",
                                "Maximum number of geometry frames in one reference window, including its full Draco keyframe."),
                            maximumGeometryKeyframeInterval);
                }
            }

            EncodingSettings effective = GetEncodingSettings();
            EditorGUILayout.HelpBox(
                effective.Description + "\n" +
                String.Format(
                    CultureInfo.InvariantCulture,
                    "{0}, CRF {1}, keyframes {2}; Draco speed {3}/{4}.",
                    effective.Codec,
                    effective.Crf,
                    effective.KeyframeInterval,
                    effective.DracoEncodeSpeed,
                    effective.DracoDecodeSpeed),
                MessageType.Info);

            showAdvanced = EditorGUILayout.Foldout(showAdvanced, "Advanced", true);
            if (showAdvanced)
            {
                using (new EditorGUI.DisabledScope(
                    encodingPreset != EncodingPreset.Custom))
                {
                    videoCodec = (VideoCodec)EditorGUILayout.EnumPopup(
                        "Video Codec", videoCodec);
                    crf = EditorGUILayout.IntSlider(
                        new GUIContent(
                            "Video Quality (CRF)",
                            "Lower values produce higher quality and larger files."),
                        crf,
                        0,
                        51);
                    keyframeInterval = EditorGUILayout.IntSlider(
                        "Keyframe Interval", keyframeInterval, 1, 300);
                    referenceFrames = EditorGUILayout.IntSlider(
                        "Reference Frames", referenceFrames, 1, 5);
                    if(videoCodec == VideoCodec.HEVC)
                    {
                        disableSao = EditorGUILayout.Toggle(
                            new GUIContent(
                                "Disable HEVC SAO",
                                "Reduces software decode work at a small quality cost."),
                            disableSao);
                    }
                    positionQuantization = EditorGUILayout.IntSlider(
                        "Position Quantization", positionQuantization, 1, 30);
                    normalQuantization = EditorGUILayout.IntSlider(
                        "Normal Quantization", normalQuantization, 1, 30);
                    textureQuantization = EditorGUILayout.IntSlider(
                        "UV Quantization", textureQuantization, 1, 30);
                    dracoEncodeSpeed = EditorGUILayout.IntSlider(
                        "Draco Encode Speed", dracoEncodeSpeed, 0, 10);
                    dracoDecodeSpeed = EditorGUILayout.IntSlider(
                        "Draco Decode Speed", dracoDecodeSpeed, 0, 10);
                }
                ffmpegPath = FileField("FFmpeg", ffmpegPath, "Select FFmpeg", "");
            }

            overwriteOutput = EditorGUILayout.Toggle(
                new GUIContent("Overwrite Output", "Replace an existing output file when Encode is clicked."),
                overwriteOutput);

            EditorGUILayout.Space();
            if (GUILayout.Button("Validate Inputs"))
            {
                ValidateAndReport();
            }
            if (GUILayout.Button("Encode Volumetric MP4", GUILayout.Height(34)))
            {
                StartEncoding();
            }
        }

        if (running)
        {
            Rect progressRect = EditorGUILayout.GetControlRect(false, 20);
            EditorGUI.ProgressBar(progressRect, progress, status);
            if (GUILayout.Button("Cancel"))
            {
                cancellation.Cancel();
                status = "Cancelling…";
            }
            Repaint();
        }
        else
        {
            EditorGUILayout.HelpBox(status, MessageType.None);
        }

        EditorGUILayout.Space();
        EditorGUILayout.LabelField("Encoder Log", EditorStyles.boldLabel);
        EditorGUILayout.TextArea(GetLogText(), GUILayout.MinHeight(180));
        EditorGUILayout.EndScrollView();
    }

    /// <summary>Validates current inputs and reports a concise result.</summary>
    private void ValidateAndReport()
    {
        try
        {
            EncodingInputs inputs = ValidateInputs();
            status = String.Format(
                CultureInfo.InvariantCulture,
                "Valid: {0} matched frames ({1}–{2}) at {3:0.###} fps.",
                inputs.Images.Count,
                inputs.Images[0].Frame,
                inputs.Images[inputs.Images.Count - 1].Frame,
                frameRate);
            AppendLog(status);
        }
        catch (Exception exception)
        {
            status = exception.Message;
            AppendLog("Validation failed: " + exception.Message);
        }
    }

    /// <summary>
    /// Validates on the main thread and runs encoding asynchronously so the
    /// editor window can continue repainting and accept cancellation.
    /// </summary>
    private async void StartEncoding()
    {
        EncodingInputs inputs;
        try
        {
            inputs = ValidateInputs();
        }
        catch (Exception exception)
        {
            status = exception.Message;
            EditorUtility.DisplayDialog("Cannot encode", exception.Message, "OK");
            return;
        }

        SaveSettings();
        cancellation = new CancellationTokenSource();
        running = true;
        progress = 0.0f;
        log.Clear();

        try
        {
            await Task.Run(() => Encode(inputs, cancellation.Token));
            status = "Encoding complete";
            progress = 1.0f;
            AssetDatabase.Refresh();
            Debug.Log(
                adaptivePackage
                    ? "Created adaptive volumetric package: " +
                        AdaptivePackageDirectory()
                    : "Created volumetric video: " + outputFile);
        }
        catch (OperationCanceledException)
        {
            status = "Encoding cancelled";
            AppendLog(status);
        }
        catch (Exception exception)
        {
            status = "Encoding failed";
            AppendLog(exception.Message);
            Debug.LogError("Volumetric video encoding failed: " + exception);
        }
        finally
        {
            running = false;
            cancellation.Dispose();
            cancellation = null;
            Repaint();
        }
    }

    /// <summary>
    /// Executes the complete image/video, OBJ/Draco, packaging, and verification
    /// pipeline on a worker thread.
    /// </summary>
    private void Encode(EncodingInputs inputs, CancellationToken token)
    {
        if(adaptivePackage)
        {
            EncodeAdaptive(inputs, token);
            return;
        }
        EncodeRepresentation(
            inputs,
            GetEncodingSettings(),
            outputFile,
            fragmentedMp4,
            token,
            0.0f,
            1.0f);
    }

    /// <summary>Encodes, packages, verifies, and publishes one representation.</summary>
    private ulong EncodeRepresentation(
        EncodingInputs inputs,
        EncodingSettings settings,
        string destinationFile,
        bool useFragments,
        CancellationToken token,
        float progressOffset,
        float progressScale)
    {
        string temporaryDirectory = Path.Combine(
            Path.GetTempPath(),
            "volumetric-video-" + Guid.NewGuid().ToString("N"));
        string dracoDirectory = Path.Combine(temporaryDirectory, "geometry");
        string mediaPath = Path.Combine(temporaryDirectory, "media.mp4");
        Directory.CreateDirectory(dracoDirectory);

        try
        {
            AppendLog("Encoding " + inputs.Geometry.Count + " OBJ frames with Draco.");
            for (int index = 0; index < inputs.Geometry.Count; ++index)
            {
                token.ThrowIfCancellationRequested();
                NumberedPath source = inputs.Geometry[index];
                string destination = Path.Combine(
                    dracoDirectory,
                    source.FrameText + ".drc");
                if (EncodeObjToDraco(
                    source.Path,
                    destination,
                    settings.PositionQuantization,
                    settings.NormalQuantization,
                    settings.TextureQuantization,
                    settings.DracoEncodeSpeed,
                    settings.DracoDecodeSpeed,
                    geometryCompression ? 1 : 0) != 1)
                {
                    string error = Marshal.PtrToStringAnsi(GetAuthoringError());
                    throw new InvalidOperationException(
                        String.IsNullOrEmpty(error)
                            ? "Native Draco encoding failed for " + source.Path
                            : error);
                }
                progress = progressOffset + progressScale *
                    0.45f * (index + 1) / inputs.Geometry.Count;
                status = "Encoding geometry " + (index + 1) + "/" + inputs.Geometry.Count;
            }

            token.ThrowIfCancellationRequested();
            status = "Encoding texture video and audio";
            progress = progressOffset + progressScale * 0.48f;
            List<string> ffmpegArguments =
                BuildFFmpegArguments(
                    inputs, mediaPath, settings, useFragments);
            RunProcess(ffmpegPath, ffmpegArguments, token);
            AppendLog(
                "Texture video encoded; geometry timing will be derived from its sample timestamps.");

            token.ThrowIfCancellationRequested();
            status = "Packaging and verifying MP4";
            progress = progressOffset + progressScale * 0.78f;

            string outputDirectory = Path.GetDirectoryName(destinationFile);
            if (!String.IsNullOrEmpty(outputDirectory))
            {
                Directory.CreateDirectory(outputDirectory);
            }
            string packagedOutput = destinationFile + ".encoding-" +
                Guid.NewGuid().ToString("N") + ".mp4";

            try
            {
                AppendLog("Calling the native OpenVolumetricAuthoring library.");
                if (PackVolumetricVideo(
                    mediaPath,
                    dracoDirectory,
                    geometryDirectory,
                    packagedOutput,
                    settings.PositionQuantization,
                    settings.NormalQuantization,
                    settings.TextureQuantization,
                    settings.DracoEncodeSpeed,
                    settings.DracoDecodeSpeed,
                    geometryCompression ? 1 : 0,
                    geometryCompression &&
                        (settings.GeometryKeyframeInterval > 0 ||
                         limitGeometryKeyframeInterval)
                        ? (settings.GeometryKeyframeInterval > 0
                            ? settings.GeometryKeyframeInterval
                            : maximumGeometryKeyframeInterval)
                        : 0,
                    useFragments ? (int)fragmentDuration : 0,
                    useFragments
                        ? FragmentFrameInterval(frameRate, fragmentDuration)
                        : 0) != 1)
                {
                    string error = Marshal.PtrToStringAnsi(GetAuthoringError());
                    throw new InvalidOperationException(
                        String.IsNullOrEmpty(error)
                            ? "Native MP4 packaging failed."
                            : error);
                }
                string report = Marshal.PtrToStringAnsi(GetAuthoringReport());
                if (!String.IsNullOrEmpty(report))
                {
                    AppendLog(report);
                }
                AppendLog("Native packaging and verification completed.");
                token.ThrowIfCancellationRequested();

                // Do not destroy an existing output until the authoring
                // library completes round-trip and seek verification.
                if (File.Exists(destinationFile))
                {
                    File.Replace(packagedOutput, destinationFile, null);
                }
                else
                {
                    File.Move(packagedOutput, destinationFile);
                }
            }
            finally
            {
                if (File.Exists(packagedOutput))
                {
                    File.Delete(packagedOutput);
                }
            }
            progress = progressOffset + progressScale;
            return GetLastGeometryPayloadBytes();
        }
        finally
        {
            try
            {
                if (Directory.Exists(temporaryDirectory))
                {
                    Directory.Delete(temporaryDirectory, true);
                }
            }
            catch (Exception exception)
            {
                AppendLog("Could not remove temporary directory: " + exception.Message);
            }
        }
    }

    /// <summary>
    /// Stages low/high representations, validates their alignment natively,
    /// then publishes both MP4s and their manifest together.
    /// </summary>
    private void EncodeAdaptive(
        EncodingInputs inputs,
        CancellationToken token)
    {
        EncodingSettings low = GetAdaptiveEncodingSettings(0);
        EncodingSettings high = GetAdaptiveEncodingSettings(1);
        string presentationId = presentationName.Trim();
        string outputDirectory = AdaptivePackageDirectory();
        string outputParent = Path.GetDirectoryName(outputDirectory);
        string stagingDirectory = Path.Combine(
            outputParent,
            "." + presentationId + "-adaptive-" +
            Guid.NewGuid().ToString("N"));
        const string lowName = "low.mp4";
        const string highName = "high.mp4";
        const string manifestName = "manifest.json";
        string stagedLow = Path.Combine(stagingDirectory, lowName);
        string stagedHigh = Path.Combine(stagingDirectory, highName);
        string stagedManifest = Path.Combine(stagingDirectory, manifestName);
        Directory.CreateDirectory(stagingDirectory);

        try
        {
            AppendLog("Encoding adaptive low representation.");
            ulong lowGeometryBytes = EncodeRepresentation(
                inputs, low, stagedLow, true, token, 0.0f, 0.47f);
            AppendLog("Encoding adaptive high representation.");
            ulong highGeometryBytes = EncodeRepresentation(
                inputs, high, stagedHigh, true, token, 0.47f, 0.47f);
            token.ThrowIfCancellationRequested();
            status = "Validating adaptive package";
            progress = 0.96f;
            string prefix = encodingPreset == EncodingPreset.QuestStreaming
                ? "quest-streaming"
                : "desktop-streaming";
            if(WriteAdaptiveManifest(
                presentationId,
                stagedManifest,
                (int)fragmentDuration,
                prefix + "-low",
                stagedLow,
                low.PositionQuantization,
                lowGeometryBytes,
                prefix + "-high",
                stagedHigh,
                high.PositionQuantization,
                highGeometryBytes,
                geometryCompression ? 1 : 0) != 1)
            {
                throw new InvalidOperationException(
                    Marshal.PtrToStringAnsi(GetAuthoringError()) ??
                    "Adaptive package validation failed.");
            }
            string verificationReport =
                Marshal.PtrToStringAnsi(GetAuthoringReport());
            if(!String.IsNullOrWhiteSpace(verificationReport))
            {
                AppendLog(verificationReport);
            }

            string finalLow = Path.Combine(outputDirectory, lowName);
            string finalHigh = Path.Combine(outputDirectory, highName);
            string finalManifest = Path.Combine(outputDirectory, manifestName);
            Directory.CreateDirectory(outputDirectory);
            foreach(string path in new[] { finalLow, finalHigh, finalManifest })
            {
                if(File.Exists(path)) File.Delete(path);
            }
            File.Move(stagedLow, finalLow);
            File.Move(stagedHigh, finalHigh);
            File.Move(stagedManifest, finalManifest);
            AppendLog("Adaptive package created: " + finalManifest);
            progress = 1.0f;
        }
        finally
        {
            if(Directory.Exists(stagingDirectory))
            {
                Directory.Delete(stagingDirectory, true);
            }
        }
    }

    /// <summary>Builds deterministic FFmpeg arguments for the selected preset.</summary>
    private List<string> BuildFFmpegArguments(
        EncodingInputs inputs,
        string mediaPath,
        EncodingSettings settings,
        bool useFragments)
    {
        NumberedPath first = inputs.Images[0];
        string imagePattern = Path.Combine(
            imageDirectory,
            "%0" + first.FrameText.Length + "d" + first.Extension);
        NativeEncodingSettings nativeSettings = settings.ToNative();
        nativeSettings.FragmentDurationSeconds =
            useFragments ? (int)fragmentDuration : 0;
        IntPtr result = BuildNativeFFmpegArguments(
            imagePattern,
            audioFile ?? String.Empty,
            mediaPath,
            frameRate,
            first.Frame,
            inputs.Images.Count,
            ref nativeSettings);
        if (result == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                Marshal.PtrToStringAnsi(GetAuthoringError()) ??
                "Could not construct FFmpeg arguments.");
        }
        return new List<string>(
            Marshal.PtrToStringAnsi(result).Split('\n'));
    }

    /// <summary>
    /// Resolves files, verifies sequence alignment, and returns immutable
    /// inputs suitable for the worker thread.
    /// </summary>
    private EncodingInputs ValidateInputs()
    {
        RequireDirectory(imageDirectory, "Image sequence");
        RequireDirectory(geometryDirectory, "OBJ sequence");
        RequireExecutable(ffmpegPath, "FFmpeg");

        if (frameRate <= 0.0f)
        {
            throw new InvalidOperationException("Frame rate must be greater than zero.");
        }
        if (fragmentedMp4)
        {
            FragmentFrameInterval(frameRate, fragmentDuration);
        }
        if(adaptivePackage &&
            encodingPreset != EncodingPreset.DesktopStreaming &&
            encodingPreset != EncodingPreset.QuestStreaming)
        {
            throw new InvalidOperationException(
                "Adaptive packages require Desktop Streaming or Quest Streaming.");
        }
        if (geometryCompression &&
            limitGeometryKeyframeInterval &&
            maximumGeometryKeyframeInterval < 1)
        {
            throw new InvalidOperationException(
                "Maximum geometry frames must be at least one.");
        }
        if (!String.IsNullOrEmpty(audioFile) && !File.Exists(audioFile))
        {
            throw new InvalidOperationException("Audio file does not exist: " + audioFile);
        }
        if (!adaptivePackage && String.IsNullOrWhiteSpace(outputFile))
        {
            throw new InvalidOperationException("Choose an output MP4.");
        }
        if (!adaptivePackage && File.Exists(outputFile) && !overwriteOutput)
        {
            throw new InvalidOperationException(
                "The output already exists. Choose another file or enable Overwrite Output.");
        }
        if(adaptivePackage)
        {
            if(String.IsNullOrWhiteSpace(adaptiveOutputFolder))
            {
                throw new InvalidOperationException(
                    "Choose an adaptive output parent folder.");
            }
            if(String.IsNullOrWhiteSpace(presentationName) ||
                presentationName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            {
                throw new InvalidOperationException(
                    "Choose a valid presentation name.");
            }
            string directory = AdaptivePackageDirectory();
            string[] outputs = {
                Path.Combine(directory, "low.mp4"),
                Path.Combine(directory, "high.mp4"),
                Path.Combine(directory, "manifest.json")};
            if(!overwriteOutput && outputs.Any(File.Exists))
            {
                throw new InvalidOperationException(
                    "An adaptive package output exists. Enable Overwrite Output or choose another base name.");
            }
        }
        if (ValidateNativeSources(imageDirectory, geometryDirectory) < 0)
        {
            throw new InvalidOperationException(
                Marshal.PtrToStringAnsi(GetAuthoringError()) ??
                "Image and OBJ validation failed.");
        }

        List<NumberedPath> images = DiscoverNumberedFiles(
            imageDirectory,
            new[] { ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".exr" },
            "images");
        List<NumberedPath> geometry = DiscoverNumberedFiles(
            geometryDirectory,
            new[] { ".obj" },
            "OBJ meshes");

        if (images.Count != geometry.Count ||
            !images.Select(item => item.Frame).SequenceEqual(
                geometry.Select(item => item.Frame)))
        {
            throw new InvalidOperationException(
                "Image and OBJ frame numbers must match exactly.");
        }
        return new EncodingInputs(images, geometry);
    }

    /// <summary>
    /// Converts the selected duration to an exact source-frame boundary.
    /// Fragmented authoring rejects fractional frame counts so every video
    /// and geometry access point shares an unambiguous frame index.
    /// </summary>
    private static int FragmentFrameInterval(
        double sourceFrameRate,
        FragmentDuration duration)
    {
        if(!Enum.IsDefined(typeof(FragmentDuration), duration))
        {
            throw new InvalidOperationException(
                "Fragment duration must be 1, 2, or 4 seconds.");
        }
        double exact = sourceFrameRate * (int)duration;
        int frames = (int)Math.Round(exact);
        if(frames <= 0 || Math.Abs(exact - frames) > 0.000001)
        {
            throw new InvalidOperationException(
                "Fragment duration must contain an integral number of source frames.");
        }
        return frames;
    }

    /// <summary>
    /// Finds files whose stem is an integer and returns them in frame order.
    /// </summary>
    private static List<NumberedPath> DiscoverNumberedFiles(
        string directory,
        IReadOnlyCollection<string> extensions,
        string label)
    {
        List<NumberedPath> files = new List<NumberedPath>();
        foreach (string path in Directory.EnumerateFiles(directory))
        {
            string extension = Path.GetExtension(path);
            if (!extensions.Contains(extension.ToLowerInvariant()))
            {
                continue;
            }

            string stem = Path.GetFileNameWithoutExtension(path);
            Match match = NumberedFile.Match(stem);
            if (!match.Success ||
                !Int32.TryParse(match.Groups["frame"].Value, out int frame))
            {
                continue;
            }
            files.Add(new NumberedPath(frame, stem, extension, path));
        }

        files.Sort((left, right) => left.Frame.CompareTo(right.Frame));
        if (files.Count == 0)
        {
            throw new InvalidOperationException(
                "No numbered " + label + " found in " + directory + ".");
        }
        return files;
    }

    /// <summary>
    /// Runs a child process, streams its diagnostics into the window log, and
    /// converts cancellation or non-zero exit into a useful exception.
    /// </summary>
    private void RunProcess(
        string executable,
        IEnumerable<string> arguments,
        CancellationToken token)
    {
        string argumentString = String.Join(" ", arguments.Select(QuoteArgument));
        AppendLog("> " + executable + " " + argumentString);

        using (Process process = new Process())
        {
            process.StartInfo = new ProcessStartInfo
            {
                FileName = executable,
                Arguments = argumentString,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            process.OutputDataReceived += (_, eventArgs) =>
            {
                if (eventArgs.Data != null) AppendLog(eventArgs.Data);
            };
            process.ErrorDataReceived += (_, eventArgs) =>
            {
                if (eventArgs.Data != null) AppendLog(eventArgs.Data);
            };

            if (!process.Start())
            {
                throw new InvalidOperationException("Could not start " + executable);
            }
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            while (!process.WaitForExit(100))
            {
                if (!token.IsCancellationRequested)
                {
                    continue;
                }
                try { process.Kill(); } catch { }
                token.ThrowIfCancellationRequested();
            }
            process.WaitForExit();
            if (process.ExitCode != 0)
            {
                throw new InvalidOperationException(
                    Path.GetFileName(executable) +
                    " failed with exit code " + process.ExitCode + ".");
            }
        }
    }

    /// <summary>Appends one thread-safe line to the bounded UI log.</summary>
    private void AppendLog(string message)
    {
        lock (log)
        {
            log.AppendLine(message);
        }
    }

    /// <summary>Returns a thread-safe snapshot of the current UI log.</summary>
    private string GetLogText()
    {
        lock (log)
        {
            return log.ToString();
        }
    }

    /// <summary>Quotes one process argument without invoking a shell.</summary>
    private static string QuoteArgument(string value)
    {
        if (String.IsNullOrEmpty(value)) return "\"\"";
        if (!value.Any(character =>
            Char.IsWhiteSpace(character) || character == '"' || character == '\\'))
        {
            return value;
        }
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }

    /// <summary>Throws a validation error unless path is an existing directory.</summary>
    private static void RequireDirectory(string path, string label)
    {
        if (!Directory.Exists(path))
        {
            throw new InvalidOperationException(label + " directory does not exist.");
        }
    }

    /// <summary>Throws a validation error unless path is an executable file.</summary>
    private static void RequireExecutable(string path, string label)
    {
        if (String.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            throw new InvalidOperationException(
                label + " was not found. Set its path under Advanced.");
        }
    }

    /// <summary>Draws a text field with an adjacent directory picker.</summary>
    private static string DirectoryField(string label, string value)
    {
        EditorGUILayout.BeginHorizontal();
        value = EditorGUILayout.TextField(label, value);
        if (GUILayout.Button("Browse", GUILayout.Width(70)))
        {
            string selected = EditorUtility.OpenFolderPanel(label, value, "");
            if (!String.IsNullOrEmpty(selected)) value = selected;
        }
        EditorGUILayout.EndHorizontal();
        return value;
    }

    /// <summary>Draws an optional file picker that accepts an empty value.</summary>
    private static string OptionalFileField(
        string label,
        string value,
        string title,
        string extension)
    {
        return FileField(label, value, title, extension, true);
    }

    /// <summary>Draws a file path field and native open-file picker.</summary>
    private static string FileField(
        string label,
        string value,
        string title,
        string extension,
        bool allowClear = false)
    {
        EditorGUILayout.BeginHorizontal();
        value = EditorGUILayout.TextField(label, value);
        if (GUILayout.Button("Browse", GUILayout.Width(70)))
        {
            string directory = String.IsNullOrEmpty(value)
                ? Application.dataPath
                : Path.GetDirectoryName(value);
            string selected = EditorUtility.OpenFilePanel(title, directory, extension);
            if (!String.IsNullOrEmpty(selected)) value = selected;
        }
        if (allowClear && GUILayout.Button("Clear", GUILayout.Width(50)))
        {
            value = "";
        }
        EditorGUILayout.EndHorizontal();
        return value;
    }

    /// <summary>Draws an MP4 destination field and save-file picker.</summary>
    private static string SaveFileField(string label, string value)
    {
        EditorGUILayout.BeginHorizontal();
        value = EditorGUILayout.TextField(label, value);
        if (GUILayout.Button("Browse", GUILayout.Width(70)))
        {
            string directory = String.IsNullOrEmpty(value)
                ? Path.Combine(Application.dataPath, "StreamingAssets")
                : Path.GetDirectoryName(value);
            string selected = EditorUtility.SaveFilePanel(
                "Output volumetric MP4", directory, "volumetric_video", "mp4");
            if (!String.IsNullOrEmpty(selected)) value = selected;
        }
        EditorGUILayout.EndHorizontal();
        return value;
    }

    /// <summary>Returns the initial OpenVolumetric output path within the project.</summary>
    private static string DefaultOutputPath()
    {
        return Path.Combine(
            Application.dataPath,
            "StreamingAssets",
            "openvolumetric.mp4");
    }

    /// <summary>Returns the default parent for adaptive presentation folders.</summary>
    private static string DefaultAdaptiveOutputFolder()
    {
        return Path.Combine(Application.dataPath, "StreamingAssets");
    }

    /// <summary>Returns the normalized destination for the adaptive package.</summary>
    private string AdaptivePackageDirectory()
    {
        return Path.Combine(
            Path.GetFullPath(adaptiveOutputFolder),
            presentationName.Trim());
    }

    /// <summary>Searches PATH and common locations for an external executable.</summary>
    private static string FindExecutable(string name)
    {
        string path = Environment.GetEnvironmentVariable("PATH") ?? "";
        string executable = Application.platform == RuntimePlatform.WindowsEditor
            ? name + ".exe"
            : name;
        foreach (string directory in path.Split(Path.PathSeparator))
        {
            string candidate = Path.Combine(directory, executable);
            if (File.Exists(candidate)) return candidate;
        }
        return "";
    }

    /// <summary>Loads one editor preference or returns fallback.</summary>
    private static string Load(string key, string fallback)
    {
        return EditorPrefs.GetString(PreferencePrefix + key, fallback ?? "");
    }

    /// <summary>Persists user-selected paths between editor sessions.</summary>
    private void SaveSettings()
    {
        EditorPrefs.SetString(PreferencePrefix + "Images", imageDirectory);
        EditorPrefs.SetString(PreferencePrefix + "Geometry", geometryDirectory);
        EditorPrefs.SetString(PreferencePrefix + "Audio", audioFile);
        EditorPrefs.SetString(PreferencePrefix + "Output", outputFile);
        EditorPrefs.SetString(
            PreferencePrefix + "AdaptiveOutputFolder", adaptiveOutputFolder);
        EditorPrefs.SetString(
            PreferencePrefix + "PresentationName", presentationName);
        EditorPrefs.SetString(PreferencePrefix + "FFmpeg", ffmpegPath);
        EditorPrefs.SetBool(
            PreferencePrefix + "GeometryCompression", geometryCompression);
        EditorPrefs.SetBool(
            PreferencePrefix + "LimitGeometryKeyframeInterval",
            limitGeometryKeyframeInterval);
        EditorPrefs.SetInt(
            PreferencePrefix + "MaximumGeometryKeyframeInterval",
            maximumGeometryKeyframeInterval);
        EditorPrefs.SetBool(
            PreferencePrefix + "FragmentedMp4", fragmentedMp4);
        EditorPrefs.SetBool(
            PreferencePrefix + "AdaptivePackage", adaptivePackage);
        EditorPrefs.SetInt(
            PreferencePrefix + "FragmentDuration", (int)fragmentDuration);
    }

    /// <summary>Maps the selected platform preset to concrete codec settings.</summary>
    private EncodingSettings GetEncodingSettings()
    {
        if (encodingPreset != EncodingPreset.Custom)
        {
            NativeEncodingSettings settings = new NativeEncodingSettings();
            if (GetNativePreset((int)encodingPreset, ref settings) < 0)
            {
                throw new InvalidOperationException(
                    "Could not load the native platform preset.");
            }
            return EncodingSettings.FromNative(
                settings,
                encodingPreset == EncodingPreset.DesktopLocal
                    ? "Prioritises texture and geometry quality for local desktop playback."
                    : encodingPreset == EncodingPreset.DesktopStreaming
                        ? "Constrains bitrate and dependency windows for high-quality desktop streaming."
                        : encodingPreset == EncodingPreset.QuestLocal
                            ? "Prioritises Quest decoding speed without a network bitrate ceiling."
                            : "Constrains bitrate and reference windows for stable Quest Wi-Fi playback.");
        }
        return new EncodingSettings(
            videoCodec,
            crf,
            keyframeInterval,
            referenceFrames,
            disableSao,
            positionQuantization,
            normalQuantization,
            textureQuantization,
            dracoEncodeSpeed,
            dracoDecodeSpeed,
            0,
            0,
            0,
            "Uses the advanced settings below.");
    }

    /// <summary>Loads one shared low/high entry for adaptive authoring.</summary>
    private EncodingSettings GetAdaptiveEncodingSettings(int quality)
    {
        NativeEncodingSettings settings = new NativeEncodingSettings();
        if(GetNativeAdaptivePreset(
            (int)encodingPreset,
            quality,
            (int)fragmentDuration,
            ref settings) < 0)
        {
            throw new InvalidOperationException(
                Marshal.PtrToStringAnsi(GetAuthoringError()) ??
                "Could not load the adaptive streaming ladder.");
        }
        return EncodingSettings.FromNative(
            settings,
            quality == 0
                ? "Adaptive low representation."
                : "Adaptive high representation.");
    }

    private sealed class EncodingSettings
    {
        public readonly VideoCodec Codec;
        public readonly int Crf;
        public readonly int KeyframeInterval;
        public readonly int ReferenceFrames;
        public readonly bool DisableSao;
        public readonly int PositionQuantization;
        public readonly int NormalQuantization;
        public readonly int TextureQuantization;
        public readonly int DracoEncodeSpeed;
        public readonly int DracoDecodeSpeed;
        public readonly int MaximumVideoBitrateKbps;
        public readonly int VideoBufferSizeKbps;
        public readonly int GeometryKeyframeInterval;
        public readonly string Description;

        public EncodingSettings(
            VideoCodec codec,
            int crfValue,
            int keyframes,
            int references,
            bool disableSaoValue,
            int position,
            int normal,
            int texture,
            int encodeSpeed,
            int decodeSpeed,
            int maximumVideoBitrateKbps,
            int videoBufferSizeKbps,
            int geometryKeyframeInterval,
            string description)
        {
            Codec = codec;
            Crf = crfValue;
            KeyframeInterval = keyframes;
            ReferenceFrames = references;
            DisableSao = disableSaoValue;
            PositionQuantization = position;
            NormalQuantization = normal;
            TextureQuantization = texture;
            DracoEncodeSpeed = encodeSpeed;
            DracoDecodeSpeed = decodeSpeed;
            MaximumVideoBitrateKbps = maximumVideoBitrateKbps;
            VideoBufferSizeKbps = videoBufferSizeKbps;
            GeometryKeyframeInterval = geometryKeyframeInterval;
            Description = description;
        }

        public NativeEncodingSettings ToNative()
        {
            return new NativeEncodingSettings
            {
                Codec = Codec == VideoCodec.HEVC ? 0 : 1,
                Crf = Crf,
                VideoKeyframeInterval = KeyframeInterval,
                ReferenceFrames = ReferenceFrames,
                DisableSao = DisableSao ? 1 : 0,
                PositionQuantization = PositionQuantization,
                NormalQuantization = NormalQuantization,
                TextureQuantization = TextureQuantization,
                DracoEncodeSpeed = DracoEncodeSpeed,
                DracoDecodeSpeed = DracoDecodeSpeed,
                MaximumVideoBitrateKbps = MaximumVideoBitrateKbps,
                VideoBufferSizeKbps = VideoBufferSizeKbps,
                GeometryKeyframeInterval = GeometryKeyframeInterval
            };
        }

        public static EncodingSettings FromNative(
            NativeEncodingSettings settings,
            string description)
        {
            return new EncodingSettings(
                settings.Codec == 0 ? VideoCodec.HEVC : VideoCodec.H264,
                settings.Crf,
                settings.VideoKeyframeInterval,
                settings.ReferenceFrames,
                settings.DisableSao != 0,
                settings.PositionQuantization,
                settings.NormalQuantization,
                settings.TextureQuantization,
                settings.DracoEncodeSpeed,
                settings.DracoDecodeSpeed,
                settings.MaximumVideoBitrateKbps,
                settings.VideoBufferSizeKbps,
                settings.GeometryKeyframeInterval,
                description);
        }
    }

    private sealed class EncodingInputs
    {
        public readonly List<NumberedPath> Images;
        public readonly List<NumberedPath> Geometry;

        public EncodingInputs(
            List<NumberedPath> images,
            List<NumberedPath> geometry)
        {
            Images = images;
            Geometry = geometry;
        }
    }

    private sealed class NumberedPath
    {
        public readonly int Frame;
        public readonly string FrameText;
        public readonly string Extension;
        public readonly string Path;

        public NumberedPath(
            int frame,
            string frameText,
            string extension,
            string path)
        {
            Frame = frame;
            FrameText = frameText;
            Extension = extension;
            Path = path;
        }
    }
}

}
