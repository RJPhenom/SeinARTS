/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityDeterminismValidator.cpp
 * @author       RJ Macklem
 * @created      14 Aug 2026
 * @latest       14 Aug 2026
 * @brief        Implements deterministic-state validation for Ability Blueprints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Validators/SeinAbilityDeterminismValidator.h"

#include "Abilities/SeinAbility.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "SeinAbilityDeterminismValidator"

bool USeinAbilityDeterminismValidator::IsTargetBlueprint(
	UBlueprint* Blueprint) const
{
	if (!Blueprint)
	{
		return false;
	}

	const UClass* AbilityClass = USeinAbility::StaticClass();
	return (Blueprint->GeneratedClass
			&& Blueprint->GeneratedClass->IsChildOf(AbilityClass))
		|| (Blueprint->SkeletonGeneratedClass
			&& Blueprint->SkeletonGeneratedClass->IsChildOf(AbilityClass))
		|| (Blueprint->ParentClass
			&& Blueprint->ParentClass->IsChildOf(AbilityClass));
}

FText USeinAbilityDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("AbilityKind", "Ability");
}

FText USeinAbilityDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT(
		"AbilityHint", "fixed-point and deterministic ability");
}

#undef LOCTEXT_NAMESPACE
