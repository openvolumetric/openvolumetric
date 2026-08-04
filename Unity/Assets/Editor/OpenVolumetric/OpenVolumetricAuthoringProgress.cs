using System.Text;
using System.Threading;

namespace OpenVolumetric.Editor
{

/// <summary>
/// Owns the mutable, cross-thread state of one editor authoring job.
/// Worker threads publish snapshots here; the editor window only reads them.
/// </summary>
internal sealed class OpenVolumetricAuthoringProgress
{
    private readonly StringBuilder log = new StringBuilder();
    private volatile float completion;
    private volatile bool running;
    private volatile string status = "Ready";

    public CancellationTokenSource Cancellation { get; private set; }
    public float Completion { get => completion; set => completion = value; }
    public bool IsRunning { get => running; set => running = value; }
    public string Status { get => status; set => status = value; }

    /// <summary>Begins a new job and clears diagnostics from the prior run.</summary>
    public CancellationToken Begin()
    {
        Cancellation?.Dispose();
        Cancellation = new CancellationTokenSource();
        completion = 0.0f;
        running = true;
        status = "Starting";
        lock (log) log.Clear();
        return Cancellation.Token;
    }

    /// <summary>Requests cooperative cancellation without blocking the UI.</summary>
    public void Cancel()
    {
        Cancellation?.Cancel();
        status = "Cancelling…";
    }

    /// <summary>Releases the cancellation source after a completed job.</summary>
    public void End()
    {
        running = false;
        Cancellation?.Dispose();
        Cancellation = null;
    }

    public void Append(string message)
    {
        lock (log) log.AppendLine(message);
    }

    public string LogSnapshot()
    {
        lock (log) return log.ToString();
    }
}

}
