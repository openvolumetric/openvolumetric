#include "OpenVolumetricSourceResolver.h"

#include "Misc/Paths.h"

bool FOpenVolumetricSourceResolver::Resolve(
	const FOpenVolumetricSourceRequest& Request,
	FOpenVolumetricResolvedSource& Result)
{
	Result = FOpenVolumetricResolvedSource();
	const FString TrimmedUrl = Request.Url.TrimStartAndEnd();
	Result.bRemote = !TrimmedUrl.IsEmpty();
	if (!Result.bRemote && Request.FilePath.IsEmpty())
	{
		Result.Error = TEXT("No OpenVolumetric input has been selected.");
		return false;
	}

	Result.Resource = Result.bRemote ? TrimmedUrl : Request.FilePath;
	if (Result.bRemote)
	{
		if (!Result.Resource.StartsWith(
				TEXT("http://"), ESearchCase::IgnoreCase) &&
			!Result.Resource.StartsWith(
				TEXT("https://"), ESearchCase::IgnoreCase))
		{
			Result.Error = TEXT("SourceUrl must be an HTTP or HTTPS URL.");
			return false;
		}
	}
	else if (!ResolveLocalPath(Result.Resource))
	{
		Result.Error = FString::Printf(
			TEXT("OpenVolumetric input does not exist: %s"),
			*Result.Resource);
		return false;
	}

	if (!Request.bUseAdaptiveManifest)
	{
		return true;
	}

	openvolumetric::AdaptiveSelection Selection;
	std::string NativeError;
	const FTCHARToUTF8 Utf8Manifest(*Result.Resource);
	if (!openvolumetric::load_adaptive_representation(
			Utf8Manifest.Get(),
			Request.AdaptiveQuality,
			Request.CapabilityLimits,
			Selection,
			NativeError))
	{
		Result.Error = UTF8_TO_TCHAR(NativeError.c_str());
		return false;
	}

	Result.Resource = UTF8_TO_TCHAR(Selection.resolved_resource.c_str());
	Result.RepresentationId = UTF8_TO_TCHAR(
		Selection.representation.id.c_str());
	Result.MeasuredThroughputMbps =
		static_cast<double>(Selection.measured_throughput_bps) / 1000000.0;
	Result.DecisionReason = UTF8_TO_TCHAR(Selection.decision_reason.c_str());
	Result.SegmentDuration = Selection.manifest.segment_duration_seconds;
	for (const openvolumetric::ResolvedAdaptiveRepresentation& Entry :
		Selection.eligible_representations)
	{
		FOpenVolumetricResolvedRepresentation RuntimeEntry;
		RuntimeEntry.Id = UTF8_TO_TCHAR(Entry.representation.id.c_str());
		RuntimeEntry.Resource = UTF8_TO_TCHAR(Entry.resolved_resource.c_str());
		RuntimeEntry.Bandwidth = Entry.representation.bandwidth;
		Result.Representations.Add(MoveTemp(RuntimeEntry));
	}
	if (!Result.bRemote && !FPaths::FileExists(Result.Resource))
	{
		Result.Error = FString::Printf(
			TEXT("Adaptive representation does not exist: %s"),
			*Result.Resource);
		return false;
	}
	return true;
}

bool FOpenVolumetricSourceResolver::ResolveLocalPath(FString& Path)
{
	FPaths::NormalizeFilename(Path);
	if (FPaths::IsRelative(Path))
	{
		const TArray<FString> Candidates = {
			FPaths::ConvertRelativePathToFull(Path),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path),
			FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir(), Path),
			FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir(), FPaths::GetCleanFilename(Path))
		};
		for (const FString& Candidate : Candidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				Path = Candidate;
				break;
			}
		}
	}
	FPaths::NormalizeFilename(Path);
	return FPaths::FileExists(Path);
}
