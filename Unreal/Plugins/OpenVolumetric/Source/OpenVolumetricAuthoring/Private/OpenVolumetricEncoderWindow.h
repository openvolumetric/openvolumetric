#pragma once

#include "Widgets/SCompoundWidget.h"

/** Slate front end for the native OpenVolumetric authoring pipeline. */
class SOpenVolumetricEncoderWindow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVolumetricEncoderWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Arguments);
	virtual ~SOpenVolumetricEncoderWindow() override;

private:
	class FImpl;
	FImpl* Impl = nullptr;
};
