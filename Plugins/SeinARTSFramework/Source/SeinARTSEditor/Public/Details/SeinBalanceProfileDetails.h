/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileDetails.h
 * @brief   Details customization for USeinBalanceProfile. Adds a top "Balance"
 *          category with the Preview / Gather / Push action buttons. Mirrors the
 *          movement-mode "Sync Tuning" button pattern (FSeinMovementModeDetails):
 *          the action logic that needs editor-only synthesis lives here in the
 *          editor module.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class USeinBalanceProfile;

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

	/** The profile being customized. */
	TWeakObjectPtr<USeinBalanceProfile> WeakProfile;

	/** Last computed preview text, shown below the buttons. Updated on Preview. */
	FText PreviewText;
};
