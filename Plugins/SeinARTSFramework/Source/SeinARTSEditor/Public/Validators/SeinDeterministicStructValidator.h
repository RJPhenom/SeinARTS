/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDeterministicStructValidator.h
 * @brief   Post-change listener on the UDS editor. When a field with a
 *          non-deterministic type is added or retyped on a SeinDeterministic-
 *          marked UDS, the validator removes it and surfaces a Slate toast.
 *          Picker-level filtering is a UE limitation, so
 *          we enforce after selection instead of before.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet2/StructureEditorUtils.h"

class FSeinDeterministicStructValidator : public FStructureEditorUtils::INotifyOnStructChanged
{
public:
	virtual void PreChange(const UUserDefinedStruct* Changed, FStructureEditorUtils::EStructureEditorChangeInfo ChangeInfo) override;
	virtual void PostChange(const UUserDefinedStruct* Changed, FStructureEditorUtils::EStructureEditorChangeInfo ChangeInfo) override;
};
