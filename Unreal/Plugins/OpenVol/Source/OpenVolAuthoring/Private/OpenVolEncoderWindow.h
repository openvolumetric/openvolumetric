#pragma once

#include "Widgets/SCompoundWidget.h"

/** Slate front end for the native OpenVol authoring pipeline. */
class SOpenVolEncoderWindow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVolEncoderWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Arguments);
	virtual ~SOpenVolEncoderWindow() override;

private:
	class FImpl;
	FImpl* Impl = nullptr;
};
