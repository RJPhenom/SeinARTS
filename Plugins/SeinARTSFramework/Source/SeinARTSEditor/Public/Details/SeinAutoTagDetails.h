/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinAutoTagDetails.h
 * @brief:   Per-BP details-panel customizations for the auto-tag-generation
 *           system. Adds a "Reset to Auto" button next to the tag field on:
 *             - USeinAbility::AbilityTag (FSeinAbilityAutoTagDetails)
 *             - USeinEffect::EffectTag (FSeinEffectAutoTagDetails)
 *             - FSeinIdentityComponent::IdentityTag (the property-type
 *               customization replaces the existing FSeinInstancedStructDetails
 *               handling for that specific struct type)
 *
 *           Click handler routes through SeinAutoTag::RegenerateAssetTag with
 *           bForceOverManual=true so designer can re-take ownership of a tag
 *           they previously edited manually.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"

class FSeinAbilityAutoTagDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

class FSeinEffectAutoTagDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

class FSeinIdentityComponentAutoTagDetails : public IPropertyTypeCustomization
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
};
