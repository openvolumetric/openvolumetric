#pragma once

#include "CoreMinimal.h"
#include "OpenVolumetricPlayerAdapter.h"

class FArchive;

/** Immutable observations supplied by the component to the CSV recorder. */
struct FOpenVolumetricMetricSample final
{
	double WallSeconds = 0.0;
	double MediaSeconds = 0.0;
	FString PlaybackState;
	FString InputState;
	bool bRebuffering = false;
	openvolumetric::AdaptiveSwitchInfo SwitchInfo;
	double ThroughputBitsPerSecond = 0.0;
	int64 DownloadedBytes = 0;
	int64 CachedBytes = 0;
	int64 HttpRequests = 0;
	int64 NetworkRecoveries = 0;
	int64 ActiveFragment = -1;
	int64 CachedFragments = 0;
	double PresentedSeconds = -1.0;
	double FrameMilliseconds = 0.0;
	uint64 EngineMemoryBytes = 0;
	FString Error;
};

/** Owns CSV lifetime and transition-derived evaluation counters. */
class FOpenVolumetricAdaptiveMetrics final
{
public:
	~FOpenVolumetricAdaptiveMetrics();
	bool Record(const FString& FileName, double IntervalSeconds,
		const FOpenVolumetricMetricSample& Sample, FString& Error);
	void Close();

private:
	bool EnsureOpen(const FString& FileName, double Now, FString& Error);
	void FinishSwitch(double Now);

	FArchive* Archive = nullptr;
	double Started = 0.0;
	double NextSample = 0.0;
	double SwitchStarted = -1.0;
	double LastSwitchLatency = -1.0;
	uint64 SwitchFailureCount = 0;
	double RebufferStarted = -1.0;
	double TotalRebufferTime = 0.0;
	uint64 RebufferCount = 0;
	int32 PreviousSwitchState = 0;
	bool bPreviouslyRebuffering = false;
};
