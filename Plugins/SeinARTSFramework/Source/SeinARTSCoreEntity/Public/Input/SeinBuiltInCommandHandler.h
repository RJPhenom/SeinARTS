/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinBuiltInCommandHandler.h
 * @brief Stateless bridge from registered framework schemas to core dispatch.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "SeinBuiltInCommandHandler.generated.h"

/** Shared native implementation for the framework's exact built-in schemas. */
UCLASS(Const, NotBlueprintable)
class SEINARTSCOREENTITY_API USeinBuiltInCommandHandler : public USeinCommandHandler
{
	GENERATED_BODY()

public:
	virtual bool ExecuteCommand_Implementation(
		USeinWorldSubsystem* World,
		const FSeinCommand& Command,
		FGameplayTag& OutRejectionReason) const override;
};
