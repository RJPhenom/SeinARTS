/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionStampDetails.cpp
 *
 * Custom details panel for FSeinVisionStamp. Two responsibilities:
 *
 *   1. Flatten the inner `FSeinStampShape` struct (Shape, Enabled,
 *      LocalOffset, YawOffsetDegrees, plus the per-shape params) directly
 *      into the parent's child list. Without this, designers see a
 *      redundant "Shape" header above the inner fields. The struct already
 *      has `meta = (ShowOnlyInnerProperties)` on FSeinVisionStamp::Shape,
 *      but UE's customization pipeline doesn't auto-flatten when a
 *      property-type customization is registered — we walk the inner
 *      handles ourselves.
 *
 *   2. Conditional visibility on the per-shape params (Radius, HalfExtent*,
 *      ConeAngleDegrees, ConeLength, bConeRoundEdge) based on the Shape
 *      enum value. Replaces the previous EditCondition + EditConditionHides
 *      metas that were dropped — they spammed LogEditCondition errors
 *      because UE's EditCondition resolver can't reach sibling property
 *      names through FInstancedStruct's type-erasure wrapper. Doing the
 *      conditional in code is noise-free and produces the same UX.
 */

#include "Details/SeinVisionStampDetails.h"
#include "Details/SeinLayerMaskUI.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"

#include "Stamping/SeinStampShape.h"   // ESeinStampShape

#define LOCTEXT_NAMESPACE "SeinVisionStampDetails"

namespace SeinVisionStampDetailsLocal
{
	/** Returns true if the named property is relevant given the current Shape
	 *  enum value. "Universal" fields (Shape, Enabled, LocalOffset,
	 *  YawOffsetDegrees) are always relevant. The per-shape params gate on
	 *  the Shape value. */
	static bool ShouldShowShapeField(FName FieldName, ESeinStampShape Shape)
	{
		// Always-visible inner fields.
		if (FieldName == TEXT("Shape"))             return true;
		if (FieldName == TEXT("bEnabled"))          return true;
		if (FieldName == TEXT("LocalOffset"))       return true;
		if (FieldName == TEXT("YawOffsetDegrees"))  return true;

		// Per-shape params.
		switch (Shape)
		{
		case ESeinStampShape::Radial:
			return FieldName == TEXT("Radius");
		case ESeinStampShape::Rect:
			return FieldName == TEXT("HalfExtentX") || FieldName == TEXT("HalfExtentY");
		case ESeinStampShape::Conical:
			return FieldName == TEXT("ConeAngleDegrees")
				|| FieldName == TEXT("ConeLength")
				|| FieldName == TEXT("bConeRoundEdge");
		default:
			return false;
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FSeinVisionStampDetails::MakeInstance()
{
	return MakeShared<FSeinVisionStampDetails>();
}

void FSeinVisionStampDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	// As an array element, the header normally shows "Index [N]" — preserve
	// that. Only skip when the property is being inlined via
	// ShowOnlyInnerProperties (rare for this type, defensive).
	const bool bShowOnlyInner = PropertyHandle->HasMetaData(TEXT("ShowOnlyInnerProperties"));
	if (!bShowOnlyInner)
	{
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
	}
}

void FSeinVisionStampDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyUtilities> PropUtils = CustomizationUtils.GetPropertyUtilities();

	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid() || !ChildHandle->GetProperty()) continue;

		const FName PropName = ChildHandle->GetProperty()->GetFName();

		// Flatten the inner FSeinStampShape struct — instead of adding it as
		// a collapsible row (which gives the redundant "Shape" header), walk
		// its inner properties and add only the ones relevant to the current
		// Shape enum value.
		if (PropName == TEXT("Shape"))
		{
			// Resolve the Shape enum value via the Shape struct's "Shape"
			// child handle (the enum-typed field inside FSeinStampShape).
			TSharedPtr<IPropertyHandle> ShapeEnumHandle = ChildHandle->GetChildHandle(TEXT("Shape"));
			ESeinStampShape ShapeValue = ESeinStampShape::Radial; // default if read fails
			if (ShapeEnumHandle.IsValid())
			{
				uint8 RawValue = 0;
				if (ShapeEnumHandle->GetValue(RawValue) == FPropertyAccess::Success)
				{
					ShapeValue = static_cast<ESeinStampShape>(RawValue);
				}
				// Refresh the panel when Shape changes — flips the conditional
				// row set immediately so designers see the right fields.
				if (PropUtils.IsValid())
				{
					ShapeEnumHandle->SetOnPropertyValueChanged(
						FSimpleDelegate::CreateLambda([PropUtils]()
						{
							PropUtils->ForceRefresh();
						}));
				}
			}

			// Walk FSeinStampShape's children, conditionally add by name.
			uint32 NumShapeChildren = 0;
			ChildHandle->GetNumChildren(NumShapeChildren);
			for (uint32 j = 0; j < NumShapeChildren; ++j)
			{
				TSharedPtr<IPropertyHandle> InnerHandle = ChildHandle->GetChildHandle(j);
				if (!InnerHandle.IsValid() || !InnerHandle->GetProperty()) continue;
				const FName InnerName = InnerHandle->GetProperty()->GetFName();
				if (!SeinVisionStampDetailsLocal::ShouldShowShapeField(InnerName, ShapeValue))
				{
					continue;
				}
				ChildBuilder.AddProperty(InnerHandle.ToSharedRef());
			}
			continue;
		}

		// LayerMask → labeled combo using plugin-settings vision layer names.
		if (PropName == TEXT("LayerMask"))
		{
			SeinLayerMaskUI::AddLayerMaskRow(
				ChildHandle.ToSharedRef(), ChildBuilder,
				&SeinLayerMaskUI::GetFogLayerLabel,
				LOCTEXT("LayerMaskRow", "Layer Mask"));
			continue;
		}

		// Any other future top-level FSeinVisionStamp field — default render.
		ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
