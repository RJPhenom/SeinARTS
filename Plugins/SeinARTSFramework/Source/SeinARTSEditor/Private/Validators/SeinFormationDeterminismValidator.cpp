/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationDeterminismValidator.cpp
 */

#include "Validators/SeinFormationDeterminismValidator.h"
#include "Formations/SeinFormation.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "SeinFormationDeterminismValidator"

bool USeinFormationDeterminismValidator::IsTargetBlueprint(UBlueprint* Blueprint) const
{
	return Blueprint
		&& Blueprint->ParentClass
		&& Blueprint->ParentClass->IsChildOf(USeinFormation::StaticClass());
}

FText USeinFormationDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("FormationKind", "Formation");
}

FText USeinFormationDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("FormationHint", "formation toolkit");
}

#undef LOCTEXT_NAMESPACE
