/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityVariableDetails.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Adds the Expose on Activate control to ability variables.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
class IBlueprintEditor;
class FSeinAbilityVariableDetails : public IDetailCustomization
{
public:
	explicit FSeinAbilityVariableDetails(TSharedPtr<IBlueprintEditor> InEditor) : Editor(InEditor) {}
	virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
private:
	TWeakPtr<IBlueprintEditor> Editor;
};
