/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinBalanceProfileDetails.h
 * @author       RJ Macklem
 * @created      24 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Declares the Balance Data details customization, including its
 *               workflow actions and eligible component-type picker.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class SComboButton;
class SWidget;
class USeinBalanceProfile;
class UScriptStruct;

namespace SeinBalanceProfileDetails
{
	/** Collect the exact component types offered by the explicit profile picker.
	 *  Native components come from loaded framework/extension modules; designer
	 *  components come from the profile's currently matched entity defaults. */
	SEINARTSEDITOR_API void CollectTrackedComponentCandidates(
		const USeinBalanceProfile& Profile,
		TArray<const UScriptStruct*>& OutCandidates);
}

class FSeinBalanceProfileDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnPreviewClicked();
	FReply OnGatherClicked();
	FReply OnPushClicked();
	FReply OnCheckSyncClicked();
	FText GetPreviewText() const;
	TSharedRef<SWidget> GenerateTrackedComponentPicker();
	void OnTrackedComponentPicked(const UScriptStruct* InStruct);
	EVisibility GetTrackedComponentPickerVisibility() const;

	/** The profile being customized. */
	TWeakObjectPtr<USeinBalanceProfile> WeakProfile;

	/** Last computed preview text, shown below the buttons. Updated on Preview. */
	FText PreviewText;

	/** Property-editor handles used by the designer-component picker. */
	TSharedPtr<IPropertyHandle> TrackedComponentsHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
	TSharedPtr<SComboButton> TrackedComponentPickerButton;
};
