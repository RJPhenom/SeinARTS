/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandDeterminismValidator.cpp
 */

#include "Validators/SeinCommandDeterminismValidator.h"

#include "Engine/Blueprint.h"
#include "Input/SeinCommandAuthorityPolicy.h"
#include "Input/SeinCommandSchemaRegistry.h"

#define LOCTEXT_NAMESPACE "SeinCommandDeterminismValidator"

bool USeinCommandDeterminismValidator::IsTargetBlueprint(UBlueprint* Blueprint) const
{
	const UClass* ParentClass = Blueprint ? Blueprint->ParentClass : nullptr;
	return ParentClass
		&& (ParentClass->IsChildOf(USeinCommandAuthorityPolicy::StaticClass())
			|| ParentClass->IsChildOf(USeinCommandHandler::StaticClass()));
}

FText USeinCommandDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("CommandStrategyKind", "Command policy/handler");
}

FText USeinCommandDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("CommandStrategyHint", "command authority view and deterministic sim");
}

#undef LOCTEXT_NAMESPACE
