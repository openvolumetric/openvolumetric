#pragma once

#include "CoreMinimal.h"

/** Shell-free external process execution used by the authoring worker. */
namespace OpenVolumetricAuthoringProcess
{
bool Run(
	const FString& Executable,
	const FString& Arguments,
	FString& OutStandardOutput,
	FString& OutStandardError,
	int32& OutReturnCode);
}
