#include "OpenVolumetricAuthoringProcess.h"

#include "HAL/PlatformProcess.h"

bool OpenVolumetricAuthoringProcess::Run(
	const FString& Executable,
	const FString& Arguments,
	FString& OutStandardOutput,
	FString& OutStandardError,
	int32& OutReturnCode)
{
	OutReturnCode = -1;
	return FPlatformProcess::ExecProcess(
		*Executable,
		*Arguments,
		&OutReturnCode,
		&OutStandardOutput,
		&OutStandardError) && OutReturnCode == 0;
}
