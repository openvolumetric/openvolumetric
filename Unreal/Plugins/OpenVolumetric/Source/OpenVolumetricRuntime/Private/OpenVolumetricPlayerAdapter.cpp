#include "OpenVolumetricPlayerAdapter.h"

#include "Containers/StringConv.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshOverlay.h"

bool FOpenVolumetricPlayerAdapter::Open(
	const FString& Path,
	FString& OutError)
{
	const FTCHARToUTF8 Utf8Path(*Path);
	if (!Player.open(Utf8Path.Get()))
	{
		OutError = UTF8_TO_TCHAR(Player.error().c_str());
		return false;
	}
	OutError.Reset();
	return true;
}

bool FOpenVolumetricPlayerAdapter::Start(FString& OutError)
{
	if (!Player.start())
	{
		OutError = UTF8_TO_TCHAR(Player.error().c_str());
		if (OutError.IsEmpty())
		{
			OutError = TEXT("OpenVolumetric decoder workers could not start.");
		}
		return false;
	}
	OutError.Reset();
	return true;
}

void FOpenVolumetricPlayerAdapter::Stop()
{
	Player.stop();
}

bool FOpenVolumetricPlayerAdapter::Seek(
	double TimeSeconds,
	FString& OutError)
{
	if (!Player.seek(TimeSeconds))
	{
		OutError = UTF8_TO_TCHAR(Player.error().c_str());
		if (OutError.IsEmpty())
		{
			OutError = TEXT("OpenVolumetric seek failed.");
		}
		return false;
	}
	OutError.Reset();
	return true;
}

void FOpenVolumetricPlayerAdapter::SetActiveRepresentationId(
	const FString& RepresentationId)
{
	const FTCHARToUTF8 Utf8Representation(*RepresentationId);
	Player.set_active_representation_id(Utf8Representation.Get());
}

openvolumetric::AdaptiveSwitchInfo
FOpenVolumetricPlayerAdapter::GetAdaptiveSwitchInfo() const
{
	return Player.switch_info();
}

void FOpenVolumetricPlayerAdapter::ClearAdaptivePolicy()
{
	PolicyRepresentations.clear();
	Policy.configure({});
}

void FOpenVolumetricPlayerAdapter::AddAdaptivePolicyRepresentation(
	const FString& Id,
	const FString& Resource,
	uint64 Bandwidth)
{
	const FTCHARToUTF8 Utf8Id(*Id);
	const FTCHARToUTF8 Utf8Resource(*Resource);
	PolicyRepresentations.push_back(
		{Utf8Id.Get(), Utf8Resource.Get(), Bandwidth});
	Policy.configure(PolicyRepresentations);
}

namespace
{

openvolumetric::AdaptivePolicySwitchState ToPolicySwitchState(
	openvolumetric::AdaptiveSwitchState State)
{
	return static_cast<openvolumetric::AdaptivePolicySwitchState>(State);
}

openvolumetric::AdaptivePolicyInputState ToPolicyInputState(
	openvolumetric::ByteSourceState State)
{
	if (State == openvolumetric::ByteSourceState::Ready ||
		State == openvolumetric::ByteSourceState::Ended)
	{
		return openvolumetric::AdaptivePolicyInputState::Ready;
	}
	return static_cast<openvolumetric::AdaptivePolicyInputState>(State);
}

} // namespace

openvolumetric::AdaptivePolicyDecision
FOpenVolumetricPlayerAdapter::UpdateAdaptivePolicy(
	double Now,
	double PresentationTime,
	double Duration,
	double SegmentDuration)
{
	const openvolumetric::AdaptiveSwitchInfo Switch = Player.switch_info();
	const openvolumetric::OpenVolumetricBufferInfo Buffer = Player.buffer_info();
	openvolumetric::AdaptivePolicyObservation Observation;
	Observation.now = Now;
	Observation.presentation_time = PresentationTime;
	Observation.duration = Duration;
	Observation.segment_duration = SegmentDuration;
	Observation.input_state = ToPolicyInputState(Buffer.state);
	Observation.transfer_throughput_bps =
		Buffer.transfer_throughput_bits_per_second;
	Observation.downloaded_bytes = Buffer.downloaded_bytes;
	Observation.switch_state = ToPolicySwitchState(Switch.state);
	Observation.switch_generation = Switch.generation;
	Observation.switch_count = Switch.switch_count;
	Observation.active_representation = Switch.active_representation;
	openvolumetric::AdaptivePolicyDecision Decision = Policy.update(Observation);
	if (Decision.cancel_failed_switch)
		Player.cancel_pending_switch();
	if (Decision.action == openvolumetric::AdaptivePolicyAction::Switch &&
		!Player.request_switch(
			Decision.target_resource,
			Decision.target_representation,
			Decision.boundary_time,
			Decision.reason))
	{
		Decision.action = openvolumetric::AdaptivePolicyAction::RetryLater;
	}
	return Decision;
}

openvolumetric::AdaptivePolicyDecision
FOpenVolumetricPlayerAdapter::RequestAdaptivePolicy(
	int32 TargetIndex,
	double Now,
	double PresentationTime,
	double Duration,
	double SegmentDuration)
{
	const openvolumetric::AdaptiveSwitchInfo Switch = Player.switch_info();
	openvolumetric::AdaptivePolicyObservation Observation;
	Observation.now = Now;
	Observation.presentation_time = PresentationTime;
	Observation.duration = Duration;
	Observation.segment_duration = SegmentDuration;
	Observation.switch_state = ToPolicySwitchState(Switch.state);
	Observation.switch_generation = Switch.generation;
	Observation.switch_count = Switch.switch_count;
	Observation.active_representation = Switch.active_representation;
	openvolumetric::AdaptivePolicyDecision Decision = Policy.request(
		static_cast<std::size_t>(TargetIndex), Observation);
	if (Decision.action == openvolumetric::AdaptivePolicyAction::Switch &&
		!Player.request_switch(
			Decision.target_resource,
			Decision.target_representation,
			Decision.boundary_time,
			Decision.reason))
	{
		Decision.action = openvolumetric::AdaptivePolicyAction::RetryLater;
	}
	return Decision;
}

double FOpenVolumetricPlayerAdapter::GetAdaptivePolicyThroughput() const
{
	return Policy.smoothed_throughput_bps();
}

FString FOpenVolumetricPlayerAdapter::GetError() const
{
	// Keep the copied core string alive for the complete UTF-8 conversion.
	const std::string Error = Player.error();
	return UTF8_TO_TCHAR(Error.c_str());
}

void FOpenVolumetricPlayerAdapter::Close()
{
	Player.close();
}

const openvolumetric::OpenVolumetricMediaInfo&
FOpenVolumetricPlayerAdapter::GetMediaInfo() const
{
	return Player.media_info();
}

openvolumetric::OpenVolumetricBufferInfo
FOpenVolumetricPlayerAdapter::GetBufferInfo() const
{
	return Player.buffer_info();
}

openvolumetric::FrameMatchResult FOpenVolumetricPlayerAdapter::PollPresentation(
	double TimeSeconds,
	double GeometryScale,
	float LuminanceCorrection,
	float BlueProjectionCorrection,
	float RedProjectionCorrection,
	UE::Geometry::FDynamicMesh3& OutMesh,
	FVector& OutGeometryCentroid,
	TArray<FColor>& OutPixels,
	int32& OutWidth,
	int32& OutHeight,
	double& OutPresentationTime)
{
	openvolumetric::OpenVolumetricPresentation Presentation;
	const openvolumetric::FrameMatchResult Result =
		Player.presentation(TimeSeconds, Presentation);
	if (Result != openvolumetric::FrameMatchResult::Ready)
	{
		return Result;
	}

	OutMesh.Clear();
	// DynamicMeshComponent renders normal and UV overlays, rather than the
	// optional per-vertex channels on FDynamicMesh3. Create one overlay
	// element per decoded vertex and attach those elements to each triangle.
	OutMesh.EnableAttributes();
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay =
		OutMesh.Attributes()->PrimaryNormals();
	UE::Geometry::FDynamicMeshUVOverlay* UVOverlay =
		OutMesh.Attributes()->PrimaryUV();
	TArray<int32> NormalElements;
	TArray<int32> UVElements;
	NormalElements.Reserve(
		static_cast<int32>(Presentation.mesh.verts.size()));
	UVElements.Reserve(
		static_cast<int32>(Presentation.mesh.verts.size()));
	FVector3d PositionSum = FVector3d::ZeroVector;
	for (const openvolumetric::Vertex& Vertex : Presentation.mesh.verts)
	{
		// OpenVolumetric coordinates are metres with Y up. Unreal uses centimetres
		// with Z up, so rotate the axes into Unreal's coordinate convention.
		const FVector3d UnrealPosition(
			Vertex.pos[2] * GeometryScale,
			Vertex.pos[0] * GeometryScale,
			Vertex.pos[1] * GeometryScale);
		PositionSum += UnrealPosition;
		const int VertexId = OutMesh.AppendVertex(UnrealPosition);
		const int32 NormalElement = NormalOverlay->AppendElement(FVector3f(
			Vertex.normal[2],
			Vertex.normal[0],
			Vertex.normal[1]));
		NormalOverlay->SetParentVertex(NormalElement, VertexId);
		NormalElements.Add(NormalElement);

		// OpenVolumetric's source convention has its texture origin at the opposite
		// vertical edge from Unreal's material UV convention.
		const int32 UVElement = UVOverlay->AppendElement(FVector2f(
			Vertex.uv[0],
			1.0f - Vertex.uv[1]));
		UVOverlay->SetParentVertex(UVElement, VertexId);
		UVElements.Add(UVElement);
	}
	OutGeometryCentroid = Presentation.mesh.verts.empty()
		? FVector::ZeroVector
		: FVector(PositionSum / Presentation.mesh.verts.size());

	const std::vector<int>& Indices = Presentation.mesh.indexes;
	for (std::size_t Index = 0; Index + 2 < Indices.size(); Index += 3)
	{
		const int A = Indices[Index];
		// Preserve the source face order.
		const int B = Indices[Index + 1];
		const int C = Indices[Index + 2];
		if (OutMesh.IsVertex(A) &&
			OutMesh.IsVertex(B) &&
			OutMesh.IsVertex(C))
		{
			const int32 TriangleId = OutMesh.AppendTriangle(A, B, C);
			if (TriangleId >= 0)
			{
				NormalOverlay->SetTriangle(
					TriangleId,
					UE::Geometry::FIndex3i(
						NormalElements[A],
						NormalElements[B],
						NormalElements[C]));
				UVOverlay->SetTriangle(
					TriangleId,
					UE::Geometry::FIndex3i(
						UVElements[A],
						UVElements[B],
						UVElements[C]));
			}
		}
	}

	OutWidth = Presentation.width;
	OutHeight = Presentation.height;
	OutPixels.SetNumUninitialized(OutWidth * OutHeight);
	for (int32 Row = 0; Row < OutHeight; ++Row)
	{
		for (int32 Column = 0; Column < OutWidth; ++Column)
		{
			const int32 Pixel = Row * OutWidth + Column;
			const int32 Chroma =
				(Row / 2) * ((OutWidth + 1) / 2) + (Column / 2);
			// Match Unity's YUV2RGBA shader controls exactly: the three
			// user-facing corrections are offsets in normalized YUV space,
			// applied before the existing BT.601 conversion.
			const float Y =
				static_cast<float>(Presentation.y[Pixel]) / 255.0f +
				LuminanceCorrection;
			const float U =
				static_cast<float>(Presentation.u[Chroma]) /
					255.0f * 0.872f - 0.436f +
				BlueProjectionCorrection;
			const float V =
				static_cast<float>(Presentation.v[Chroma]) /
					255.0f * 1.230f - 0.615f +
				RedProjectionCorrection;
			const float R = FMath::Clamp(Y + 1.13983f * V, 0.0f, 1.0f);
			const float G = FMath::Clamp(
				Y - 0.39465f * U - 0.58060f * V, 0.0f, 1.0f);
			const float B = FMath::Clamp(Y + 2.03211f * U, 0.0f, 1.0f);
			OutPixels[Pixel] = FColor(
				static_cast<uint8>(R * 255.0f),
				static_cast<uint8>(G * 255.0f),
				static_cast<uint8>(B * 255.0f),
				255);
		}
	}

	OutPresentationTime = Presentation.presentation_time;
	return Result;
}

int32 FOpenVolumetricPlayerAdapter::ReadAudio(
	int32 SampleCount,
	TArray<int16>& OutSamples)
{
	const openvolumetric::OpenVolumetricMediaInfo& Info = Player.media_info();
	if (!Info.has_audio ||
		Info.audio_sample_rate <= 0 ||
		Info.audio_channels <= 0 ||
		SampleCount <= 0)
	{
		OutSamples.Reset();
		return 0;
	}

	AudioFloatBuffer.SetNumUninitialized(SampleCount, EAllowShrinking::No);
	const int32 SamplesRead =
		Player.read_audio(AudioFloatBuffer.GetData(), SampleCount);
	OutSamples.SetNumUninitialized(SamplesRead, EAllowShrinking::No);
	for (int32 Index = 0; Index < SamplesRead; ++Index)
	{
		const float Sample =
			FMath::Clamp(AudioFloatBuffer[Index], -1.0f, 1.0f);
		OutSamples[Index] = static_cast<int16>(
			FMath::RoundToInt(Sample * 32767.0f));
	}
	return SamplesRead;
}
