#include "Graph/SeinPinFactory.h"
#include "EdGraphSchema_K2.h"
#include "SGraphPin.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Rotator.h"
#include "Types/Transform.h"

namespace SeinPinColors
{
	// FFixedPoint:     #38D500 (matches UE float pin green)
	static const FLinearColor FixedPoint     = FLinearColor::FromSRGBColor(FColor(0x38, 0xD5, 0x00));
	// FFixedVector:    #FFCA23 (matches UE vector pin yellow/gold)
	static const FLinearColor FixedVector    = FLinearColor::FromSRGBColor(FColor(0xFF, 0xCA, 0x23));
	// FFixedRotator:   #A0B4FF (matches UE rotator pin light purple)
	static const FLinearColor FixedRotator   = FLinearColor::FromSRGBColor(FColor(0xA0, 0xB4, 0xFF));
	// FFixedTransform: #FB7100 (matches UE transform pin orange)
	static const FLinearColor FixedTransform = FLinearColor::FromSRGBColor(FColor(0xFB, 0x71, 0x00));
}

/**
 * Minimal SGraphPin subclass that overrides pin color. We render the default widget
 * layout and only customize the color; no need to touch the value editor.
 */
class SSeinColoredGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SSeinColoredGraphPin) {}
		SLATE_ARGUMENT(FLinearColor, OverrideColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		Color = InArgs._OverrideColor;
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

	virtual FSlateColor GetPinColor() const override
	{
		return Color;
	}

private:
	FLinearColor Color;
};

TSharedPtr<SGraphPin> FSeinPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
	{
		return nullptr;
	}

	const UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
	if (!Struct)
	{
		return nullptr;
	}

	FLinearColor OverrideColor = FLinearColor::White;
	bool bHandled = false;

	if (Struct == FFixedPoint::StaticStruct())
	{
		OverrideColor = SeinPinColors::FixedPoint;
		bHandled = true;
	}
	else if (Struct == FFixedVector::StaticStruct())
	{
		OverrideColor = SeinPinColors::FixedVector;
		bHandled = true;
	}
	else if (Struct == FFixedRotator::StaticStruct())
	{
		OverrideColor = SeinPinColors::FixedRotator;
		bHandled = true;
	}
	else if (Struct == FFixedTransform::StaticStruct())
	{
		OverrideColor = SeinPinColors::FixedTransform;
		bHandled = true;
	}

	if (!bHandled)
	{
		return nullptr;
	}

	return SNew(SSeinColoredGraphPin, Pin).OverrideColor(OverrideColor);
}
