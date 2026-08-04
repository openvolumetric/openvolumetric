using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

namespace OpenVolumetric.Editor
{

/// <summary>Filesystem validation shared by the Unity authoring UI.</summary>
internal static class OpenVolumetricAuthoringValidation
{
    private static readonly Regex NumberedFile =
        new Regex(@"^(?<frame>[0-9]+)$", RegexOptions.Compiled);

    public static void RequireDirectory(string path, string label)
    {
        if (!Directory.Exists(path))
            throw new InvalidOperationException(label + " directory does not exist.");
    }

    public static void RequireExecutable(string path, string label)
    {
        if (String.IsNullOrWhiteSpace(path) || !File.Exists(path))
            throw new InvalidOperationException(
                label + " was not found. Set its path under Advanced.");
    }

    public static List<OpenVolumetricNumberedPath> DiscoverNumberedFiles(
        string directory,
        IReadOnlyCollection<string> extensions,
        string label)
    {
        List<OpenVolumetricNumberedPath> files =
            new List<OpenVolumetricNumberedPath>();
        foreach (string path in Directory.EnumerateFiles(directory))
        {
            string extension = Path.GetExtension(path);
            if (!extensions.Contains(extension.ToLowerInvariant())) continue;

            string stem = Path.GetFileNameWithoutExtension(path);
            Match match = NumberedFile.Match(stem);
            if (!match.Success ||
                !Int32.TryParse(match.Groups["frame"].Value, out int frame))
                continue;
            files.Add(new OpenVolumetricNumberedPath(
                frame, stem, extension, path));
        }

        files.Sort((left, right) => left.Frame.CompareTo(right.Frame));
        if (files.Count == 0)
            throw new InvalidOperationException(
                "No numbered " + label + " found in " + directory + ".");
        return files;
    }

    public static int FragmentFrameInterval(double frameRate, int seconds)
    {
        if (seconds != 1 && seconds != 2 && seconds != 4)
            throw new InvalidOperationException(
                "Fragment duration must be 1, 2, or 4 seconds.");
        double exact = frameRate * seconds;
        int frames = (int)Math.Round(exact);
        if (frames <= 0 || Math.Abs(exact - frames) > 0.000001)
            throw new InvalidOperationException(
                "Fragment duration must contain an integral number of source frames.");
        return frames;
    }
}

/// <summary>Runs FFmpeg without a shell and reports its output to the job log.</summary>
internal static class OpenVolumetricAuthoringProcess
{
    public static void Run(
        string executable,
        IEnumerable<string> arguments,
        CancellationToken token,
        Action<string> appendLog)
    {
        string argumentString = String.Join(" ", arguments.Select(Quote));
        appendLog("> " + executable + " " + argumentString);
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
            process.OutputDataReceived += (_, args) =>
            {
                if (args.Data != null) appendLog(args.Data);
            };
            process.ErrorDataReceived += (_, args) =>
            {
                if (args.Data != null) appendLog(args.Data);
            };
            if (!process.Start())
                throw new InvalidOperationException("Could not start " + executable);
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            while (!process.WaitForExit(100))
            {
                if (!token.IsCancellationRequested) continue;
                try { process.Kill(); } catch { }
                token.ThrowIfCancellationRequested();
            }
            process.WaitForExit();
            if (process.ExitCode != 0)
                throw new InvalidOperationException(
                    Path.GetFileName(executable) +
                    " failed with exit code " + process.ExitCode + ".");
        }
    }

    private static string Quote(string value)
    {
        if (String.IsNullOrEmpty(value)) return "\"\"";
        if (!value.Any(character =>
            Char.IsWhiteSpace(character) || character == '"' || character == '\\'))
            return value;
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }
}

internal sealed class OpenVolumetricNumberedPath
{
    public readonly int Frame;
    public readonly string FrameText;
    public readonly string Extension;
    public readonly string Path;

    public OpenVolumetricNumberedPath(
        int frame, string frameText, string extension, string path)
    {
        Frame = frame;
        FrameText = frameText;
        Extension = extension;
        Path = path;
    }
}

}
