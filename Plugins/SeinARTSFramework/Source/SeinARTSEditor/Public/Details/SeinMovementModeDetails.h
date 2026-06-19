/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementModeDetails.h
 * @brief   Class-Defaults customization for movement-mode Blueprints (USeinMovement
 *          subclasses). Adds a "Sync Tuning Struct" button that mirrors the BP's tuning
 *          variables into its paired UDS (see SeinMovementTuningExport). Only appears for
 *          BP-generated movement modes — C++ modes (no ClassGeneratedBy) get nothing.
 *          Registered by class NAME, so the editor module keeps no link dependency on
 *          the Movement module.
 */

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class UBlueprint;

class FSeinMovementModeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnSyncClicked();

	/** The movement-mode Blueprint behind the customized CDO; null for C++ modes. */
	TWeakObjectPtr<UBlueprint> WeakBlueprint;
};
