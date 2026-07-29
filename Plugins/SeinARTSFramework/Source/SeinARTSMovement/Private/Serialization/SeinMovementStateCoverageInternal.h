/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementStateCoverageInternal.h
 * @brief   Private canonical snapshot of native movement coverage claims.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinCanonicalStateRegistry.h"

class UClass;

struct FSeinMovementStateCoverageSnapshot
{
	FName Identity;
	TArray<FString> Claims;
	TArray<FSeinCanonicalStateKey> SupplementalProviders;

	bool operator==(const FSeinMovementStateCoverageSnapshot& Other) const
	{
		return Identity == Other.Identity
			&& Claims == Other.Claims
			&& SupplementalProviders == Other.SupplementalProviders;
	}
};

bool SeinBuildMovementStateCoverageSnapshot(
	FSeinMovementStateCoverageSnapshot& OutSnapshot,
	FString& OutError,
	bool bRequireCompleteLoadedClasses = true);

bool SeinValidateMovementStateCoverageForClass(
	const UClass* ConcreteClass,
	FString& OutError);

bool SeinValidateMovementReflectedClassForCanonicalState(
	const UClass* Class,
	bool bRequireStatelessNativeLayer,
	FString& OutError);

void SeinSetMovementCoverageProviderRefreshEnabled(bool bEnabled);
