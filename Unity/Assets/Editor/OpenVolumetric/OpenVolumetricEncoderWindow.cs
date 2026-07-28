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
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outputPath);

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
        int decodeSpeed);

    [DllImport(
        AuthoringLibrary,
        EntryPoint = "openvolumetric_authoring_last_error",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr GetAuthoringError();

    private static readonly Regex NumberedFile =
        new Regex(@"^(?<frame>[0-9]+)$", RegexOptions.Compiled);

    private enum EncodingPreset
    {
        DesktopQuality,
        QuestBalanced,
        QuestPerformance,
        Custom
    }

    private enum VideoCodec
    {
        HEVC,
        H264
    }

    [SerializeField] private EncodingPreset encodingPreset =
        EncodingPreset.QuestBalanced;
    [SerializeField] private string imageDirectory = "";
    [SerializeField] private string geometryDirectory = "";
    [SerializeField] private string audioFile = "";
    [SerializeField] private string outputFile = "";
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
        ffmpegPath = Load("FFmpeg", FindExecutable("ffmpeg"));
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
            outputFile = SaveFileField("Output MP4", outputFile);

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
            Debug.Log("Created volumetric video: " + outputFile);
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
        EncodingSettings settings = GetEncodingSettings();
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
                    settings.DracoDecodeSpeed) != 1)
                {
                    string error = Marshal.PtrToStringAnsi(GetAuthoringError());
                    throw new InvalidOperationException(
                        String.IsNullOrEmpty(error)
                            ? "Native Draco encoding failed for " + source.Path
                            : error);
                }
                progress = 0.45f * (index + 1) / inputs.Geometry.Count;
                status = "Encoding geometry " + (index + 1) + "/" + inputs.Geometry.Count;
            }

            token.ThrowIfCancellationRequested();
            status = "Encoding texture video and audio";
            progress = 0.48f;
            List<string> ffmpegArguments =
                BuildFFmpegArguments(inputs, mediaPath, settings);
            RunProcess(ffmpegPath, ffmpegArguments, token);
            AppendLog(
                "Texture video encoded; geometry timing will be derived from its sample timestamps.");

            token.ThrowIfCancellationRequested();
            status = "Packaging and verifying MP4";
            progress = 0.78f;

            string outputDirectory = Path.GetDirectoryName(outputFile);
            if (!String.IsNullOrEmpty(outputDirectory))
            {
                Directory.CreateDirectory(outputDirectory);
            }
            string packagedOutput = outputFile + ".encoding-" +
                Guid.NewGuid().ToString("N") + ".mp4";

            try
            {
                AppendLog("Calling the native OpenVolumetricAuthoring library.");
                if (PackVolumetricVideo(
                    mediaPath,
                    dracoDirectory,
                    packagedOutput) != 1)
                {
                    string error = Marshal.PtrToStringAnsi(GetAuthoringError());
                    throw new InvalidOperationException(
                        String.IsNullOrEmpty(error)
                            ? "Native MP4 packaging failed."
                            : error);
                }
                AppendLog("Native packaging and verification completed.");
                token.ThrowIfCancellationRequested();

                // Do not destroy an existing output until the authoring
                // library completes round-trip and seek verification.
                if (File.Exists(outputFile))
                {
                    File.Replace(packagedOutput, outputFile, null);
                }
                else
                {
                    File.Move(packagedOutput, outputFile);
                }
            }
            finally
            {
                if (File.Exists(packagedOutput))
                {
                    File.Delete(packagedOutput);
                }
            }
            progress = 1.0f;
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

    /// <summary>Builds deterministic FFmpeg arguments for the selected preset.</summary>
    private List<string> BuildFFmpegArguments(
        EncodingInputs inputs,
        string mediaPath,
        EncodingSettings settings)
    {
        NumberedPath first = inputs.Images[0];
        string imagePattern = Path.Combine(
            imageDirectory,
            "%0" + first.FrameText.Length + "d" + first.Extension);
        List<string> arguments = new List<string>
        {
            "-hide_banner",
            "-y",
            "-framerate", frameRate.ToString("0.########", CultureInfo.InvariantCulture),
            "-start_number", first.Frame.ToString(CultureInfo.InvariantCulture),
            "-i", imagePattern
        };

        if (!String.IsNullOrEmpty(audioFile))
        {
            arguments.Add("-i");
            arguments.Add(audioFile);
        }

        arguments.AddRange(new[]
        {
            "-frames:v", inputs.Images.Count.ToString(CultureInfo.InvariantCulture),
            "-c:v", settings.Codec == VideoCodec.HEVC ? "libx265" : "libx264",
            "-crf", settings.Crf.ToString(CultureInfo.InvariantCulture),
            "-pix_fmt", "yuv420p"
        });
        if(settings.Codec == VideoCodec.HEVC)
        {
            string parameters = String.Format(
                CultureInfo.InvariantCulture,
                "keyint={0}:min-keyint=1:bframes=0:ref={1}{2}",
                settings.KeyframeInterval,
                settings.ReferenceFrames,
                settings.DisableSao ? ":no-sao=1" : "");
            arguments.Add("-x265-params");
            arguments.Add(parameters);
        }
        else
        {
            arguments.Add("-preset");
            arguments.Add("fast");
            arguments.Add("-x264-params");
            arguments.Add(String.Format(
                CultureInfo.InvariantCulture,
                "keyint={0}:min-keyint=1:bframes=0:ref={1}",
                settings.KeyframeInterval,
                settings.ReferenceFrames));
        }

        if (!String.IsNullOrEmpty(audioFile))
        {
            arguments.AddRange(new[]
            {
                "-c:a", "aac",
                "-b:a", "192k",
                "-af", "apad",
                "-shortest"
            });
        }
        else
        {
            arguments.Add("-an");
        }
        arguments.Add(mediaPath);
        return arguments;
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
        if (!String.IsNullOrEmpty(audioFile) && !File.Exists(audioFile))
        {
            throw new InvalidOperationException("Audio file does not exist: " + audioFile);
        }
        if (String.IsNullOrWhiteSpace(outputFile))
        {
            throw new InvalidOperationException("Choose an output MP4.");
        }
        if (File.Exists(outputFile) && !overwriteOutput)
        {
            throw new InvalidOperationException(
                "The output already exists. Choose another file or enable Overwrite Output.");
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
        EnsureContiguous(images);
        EnsureUniformNaming(images, "images");
        EnsureUniformNaming(geometry, "OBJ meshes");
        return new EncodingInputs(images, geometry);
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

    /// <summary>Rejects a numbered sequence containing missing frame numbers.</summary>
    private static void EnsureContiguous(IReadOnlyList<NumberedPath> files)
    {
        for (int index = 1; index < files.Count; ++index)
        {
            if (files[index].Frame != files[index - 1].Frame + 1)
            {
                throw new InvalidOperationException(
                    "Frame sequence has a gap between " +
                    files[index - 1].Frame + " and " + files[index].Frame + ".");
            }
        }
    }

    /// <summary>
    /// Ensures image and geometry sequences use matching start/count numbering.
    /// </summary>
    private static void EnsureUniformNaming(
        IReadOnlyList<NumberedPath> files,
        string label)
    {
        int width = files[0].FrameText.Length;
        string extension = files[0].Extension;
        if (files.Any(item =>
            item.FrameText.Length != width ||
            item.Extension != extension))
        {
            throw new InvalidOperationException(
                "All " + label + " must use the same zero padding and extension.");
        }
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
        EditorPrefs.SetString(PreferencePrefix + "FFmpeg", ffmpegPath);
    }

    /// <summary>Maps the selected platform preset to concrete codec settings.</summary>
    private EncodingSettings GetEncodingSettings()
    {
        switch(encodingPreset)
        {
            case EncodingPreset.DesktopQuality:
                return new EncodingSettings(
                    VideoCodec.HEVC, 20, 60, 3, false,
                    14, 10, 12, 5, 5,
                    "Prioritises texture and geometry quality for desktop playback.");
            case EncodingPreset.QuestBalanced:
                return new EncodingSettings(
                    VideoCodec.HEVC, 25, 30, 1, true,
                    14, 10, 12, 7, 9,
                    "Default Quest profile with a simpler HEVC bitstream and fast Draco decoding.");
            case EncodingPreset.QuestPerformance:
                return new EncodingSettings(
                    VideoCodec.H264, 23, 30, 1, false,
                    12, 8, 10, 8, 10,
                    "Prioritises software decoding speed at the cost of larger video files and geometry precision.");
            default:
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
                    "Uses the advanced settings below.");
        }
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
            Description = description;
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
