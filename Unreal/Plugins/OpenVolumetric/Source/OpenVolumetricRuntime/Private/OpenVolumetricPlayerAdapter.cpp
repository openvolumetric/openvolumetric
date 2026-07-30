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
	for (const openvolumetric::Vertex& Vertex : Presentation.mesh.verts)
	{
		// OpenVolumetric coordinates are metres with Y up. Unreal uses centimetres
		// with Z up, so rotate the axes into Unreal's coordinate convention.
		const int VertexId = OutMesh.AppendVertex(FVector3d(
			Vertex.pos[2] * GeometryScale,
			Vertex.pos[0] * GeometryScale,
			Vertex.pos[1] * GeometryScale));
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
