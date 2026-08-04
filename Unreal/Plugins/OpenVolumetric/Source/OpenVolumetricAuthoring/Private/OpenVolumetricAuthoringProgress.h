#pragma once

#include "CoreMinimal.h"

/** Thread-safe progress and diagnostic channel shared by Slate and a worker. */
struct FOpenVolumetricAuthoringProgress final
{
	TAtomic<bool> bRunning{false};
	TAtomic<bool> bCancel{false};
	TAtomic<float> Progress{0.0f};

	void SetStatus(const FString& Value)
	{
		FScopeLock Lock(&TextMutex);
		Status = Value;
	}

	void Append(const FString& Value)
	{
		FScopeLock Lock(&TextMutex);
		Log += Value + LINE_TERMINATOR;
	}

	FString GetStatus() const
	{
		FScopeLock Lock(&TextMutex);
		return Status;
	}

	FString GetLog() const
	{
		FScopeLock Lock(&TextMutex);
		return Log;
	}

	void Begin()
	{
		bCancel.Store(false);
		bRunning.Store(true);
		Progress.Store(0.0f);
		FScopeLock Lock(&TextMutex);
		Log.Empty();
		Status = TEXT("Starting");
	}

private:
	mutable FCriticalSection TextMutex;
	FString Status = TEXT("Ready");
	FString Log;
};
