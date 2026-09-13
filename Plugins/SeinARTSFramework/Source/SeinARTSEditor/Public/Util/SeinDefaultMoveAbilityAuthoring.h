/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinDefaultMoveAbilityAuthoring.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Shared editor selection and validation for an entity's default move ability.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"

namespace SeinDefaultMoveAbilityAuthoring
{
	SEINARTSEDITOR_API TArray<UClass*> GetGrantedClasses(const UObject& Context);
	SEINARTSEDITOR_API bool IsEligibleClass(const UClass* Class);
	SEINARTSEDITOR_API bool ValidateSelection(const UObject& Context, const UClass* Class, FText& Error);
	SEINARTSEDITOR_API bool ValidateEntity(const UObject& Context, FText& Error);
}
