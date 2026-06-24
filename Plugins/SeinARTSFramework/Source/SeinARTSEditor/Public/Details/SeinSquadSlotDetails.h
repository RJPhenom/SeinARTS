/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSlotDetails.h
 * @brief   FSeinSquadSlot type customization that hides the per-slot OffsetTransform
 *          authoring when the owning squad's chosen Formation Class doesn't use
 *          authored slot offsets (i.e. anything but the slot formation).
 *
 *          A footprint-laid formation (Grid / Wedge / Ring / ...) ignores the
 *          authored offsets, so showing the offset field there would mislead. The
 *          slot list itself (roster: Entity / cost / tags) stays visible for EVERY
 *          formation — only OffsetTransform is conditional. Pure UI: the underlying
 *          data is never touched, so switching back to the slot formation restores
 *          the authored offsets unchanged.
 *
 *          "Uses authored slot offsets" is read from the chosen formation's CDO via
 *          USeinFormation::UsesAuthoredSlotOffsets() (the slot formation returns true)
 *          — so this editor module needs no reference to the squad extension's
 *          USeinSlotFormation. An EMPTY Formation Class = the slot formation = show.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

class FSeinSquadSlotDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Walk slot -> Slots array -> FSeinSquadComponent -> FormationClass and report whether the chosen
	 *  formation lays members out at authored slot offsets. EMPTY FormationClass, or any case the chain
	 *  can't be resolved, returns true (show the offset) so the customization only ever HIDES on a
	 *  confident non-slot answer. */
	static bool OwningFormationUsesSlotOffsets(TSharedRef<IPropertyHandle> SlotHandle);
};
