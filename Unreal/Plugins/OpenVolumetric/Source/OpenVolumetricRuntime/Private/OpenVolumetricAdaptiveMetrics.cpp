#include "OpenVolumetricAdaptiveMetrics.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

namespace
{
FString Csv(const FString& Value)
{
	return TEXT("\"") + Value.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
}

void WriteLine(FArchive& Target, const FString& Line)
{
	FTCHARToUTF8 Utf8(*(Line + LINE_TERMINATOR));
	Target.Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

FString SwitchName(openvolumetric::AdaptiveSwitchState State)
{
	switch (State)
	{
	case openvolumetric::AdaptiveSwitchState::Stable: return TEXT("Stable");
	case openvolumetric::AdaptiveSwitchState::Preparing: return TEXT("Preparing");
	case openvolumetric::AdaptiveSwitchState::Ready: return TEXT("Ready");
	default: return TEXT("Failed");
	}
}
} // namespace

FOpenVolumetricAdaptiveMetrics::~FOpenVolumetricAdaptiveMetrics()
{
	Close();
}

bool FOpenVolumetricAdaptiveMetrics::Record(
	const FString& FileName,
	double IntervalSeconds,
	const FOpenVolumetricMetricSample& Sample,
	FString& Error)
{
	if (!EnsureOpen(FileName, Sample.WallSeconds, Error))
		return false;
	if (Sample.bRebuffering && !bPreviouslyRebuffering)
	{
		++RebufferCount;
		RebufferStarted = Sample.WallSeconds;
	}
	else if (!Sample.bRebuffering && bPreviouslyRebuffering &&
		RebufferStarted >= 0.0)
	{
		TotalRebufferTime += Sample.WallSeconds - RebufferStarted;
		RebufferStarted = -1.0;
	}
	bPreviouslyRebuffering = Sample.bRebuffering;
	const int32 State = static_cast<int32>(Sample.SwitchInfo.state);
	if (Sample.SwitchInfo.state ==
			openvolumetric::AdaptiveSwitchState::Preparing &&
		PreviousSwitchState != State)
		SwitchStarted = Sample.WallSeconds;
	if (Sample.SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Failed &&
		PreviousSwitchState != State)
	{
		++SwitchFailureCount;
		FinishSwitch(Sample.WallSeconds);
	}
	if (Sample.SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Stable &&
		Sample.SwitchInfo.switch_count > 0 && PreviousSwitchState != State)
		FinishSwitch(Sample.WallSeconds);
	PreviousSwitchState = State;
	if (Sample.WallSeconds < NextSample)
		return true;
	NextSample = Sample.WallSeconds + FMath::Max(0.05, IntervalSeconds);
	double RebufferSeconds = TotalRebufferTime;
	if (RebufferStarted >= 0.0)
		RebufferSeconds += Sample.WallSeconds - RebufferStarted;
	const FString Line = FString::Printf(
		TEXT("%.6f,%.6f,%s,%s,%s,%s,%s,%.6f,%lld,%lld,%lld,%lld,%lld,%lld,%llu,%.6f,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%llu,%s"),
		Sample.WallSeconds - Started, Sample.MediaSeconds,
		*Csv(Sample.PlaybackState), *Csv(Sample.InputState),
		*Csv(UTF8_TO_TCHAR(Sample.SwitchInfo.active_representation.c_str())),
		*Csv(UTF8_TO_TCHAR(Sample.SwitchInfo.pending_representation.c_str())),
		*Csv(SwitchName(Sample.SwitchInfo.state)),
		Sample.ThroughputBitsPerSecond / 1000000.0,
		Sample.DownloadedBytes, Sample.CachedBytes, Sample.HttpRequests,
		Sample.NetworkRecoveries, Sample.ActiveFragment, Sample.CachedFragments,
		RebufferCount, RebufferSeconds, Sample.SwitchInfo.switch_count,
		SwitchFailureCount, LastSwitchLatency, Sample.PresentedSeconds,
		Sample.MediaSeconds - Sample.PresentedSeconds,
		Sample.FrameMilliseconds, Sample.EngineMemoryBytes, *Csv(Sample.Error));
	WriteLine(*Archive, Line);
	Archive->Flush();
	return true;
}

void FOpenVolumetricAdaptiveMetrics::Close()
{
	if (Archive != nullptr)
	{
		Archive->Flush();
		Archive->Close();
		delete Archive;
		Archive = nullptr;
	}
	Started = 0.0;
	NextSample = 0.0;
	SwitchStarted = -1.0;
	LastSwitchLatency = -1.0;
	SwitchFailureCount = 0;
	RebufferStarted = -1.0;
	TotalRebufferTime = 0.0;
	RebufferCount = 0;
	PreviousSwitchState = 0;
	bPreviouslyRebuffering = false;
}

bool FOpenVolumetricAdaptiveMetrics::EnsureOpen(
	const FString& FileName, double Now, FString& Error)
{
	if (Archive != nullptr)
		return true;
	const FString SafeName = FPaths::GetCleanFilename(FileName.IsEmpty()
		? TEXT("openvolumetric-adaptive-metrics.csv") : FileName);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), SafeName);
	Archive = IFileManager::Get().CreateFileWriter(*Path);
	if (Archive == nullptr)
	{
		Error = FString::Printf(TEXT("Could not create adaptive metrics file: %s"), *Path);
		return false;
	}
	WriteLine(*Archive,
		TEXT("wall_seconds,media_seconds,playback_state,input_state,")
		TEXT("active_representation,pending_representation,switch_state,")
		TEXT("throughput_mbps,downloaded_bytes,cached_bytes,http_requests,")
		TEXT("network_recoveries,active_fragment,cached_fragments,")
		TEXT("rebuffer_count,rebuffer_seconds,switch_count,switch_failures,")
		TEXT("last_switch_latency_seconds,presented_seconds,av_error_seconds,")
		TEXT("frame_ms,engine_memory_bytes,error"));
	Started = Now;
	NextSample = Now;
	return true;
}

void FOpenVolumetricAdaptiveMetrics::FinishSwitch(double Now)
{
	if (SwitchStarted < 0.0)
		return;
	LastSwitchLatency = Now - SwitchStarted;
	SwitchStarted = -1.0;
}
