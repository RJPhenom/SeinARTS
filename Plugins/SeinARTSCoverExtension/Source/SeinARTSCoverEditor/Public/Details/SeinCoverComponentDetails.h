/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverComponentDetails.h
 * @brief   Property-type customization for `FSeinCoverComponent`. Injects a
 *          "Generate Slots" button into the details panel so designers can
 *          regenerate `Slots` procedurally from `Area` + the Generate
 *          parameters without dropping into C++.
 *
 *          Mechanism: an IPropertyTypeCustomization runs whenever the struct
 *          is shown in the details panel — including when it lives inside an
 *          `FInstancedStruct` element of the entity bridge's `ComponentData`
 *          array (the new authoring surface post-Phase-5). The customization
 *          renders all child properties normally + appends one extra row at
 *          the bottom of the `SeinARTS|Cover|Generate` category with the button.
 *
 *          Replaces the pre-Phase-5 IDetailCustomization that hung off the
 *          deleted `USeinCoverProviderComponent` AC. The button's effect now
 *          mutates the struct directly via the property handle, and the
 *          generator math lives on `FSeinCoverComponent::GenerateSlots` in
 *          the sim module — editor module just wraps the UI.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"

class IPropertyHandle;

class FSeinCoverComponentDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Generate-button click handler. Resolves the `FSeinCoverComponent`
	 *  instance from the cached struct handle, calls `GenerateSlots()`,
	 *  fires PropertyChanged on the Slots / bIsDirectional sub-properties
	 *  so listeners (component visualizer, viewport) refresh, and forces
	 *  a details-panel refresh so the new slot array shows up immediately. */
	FReply OnGenerateButtonClicked();

	/** Struct handle captured at customization time so the click handler can
	 *  reach the underlying `FSeinCoverComponent` instances. Weak ownership
	 *  via TSharedPtr — UE manages the handle lifetime. */
	TSharedPtr<IPropertyHandle> StructHandle;

	/** Property utilities cached at customization time so the click handler
	 *  can call ForceRefresh to repaint the details panel after generation. */
	TSharedPtr<class IPropertyUtilities> PropUtils;
};
