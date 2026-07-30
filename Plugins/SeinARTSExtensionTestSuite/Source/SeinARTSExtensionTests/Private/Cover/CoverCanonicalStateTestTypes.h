#pragma once

#include "System/SeinCoverDefault.h"
#include "System/SeinCoverSystem.h"
#include "CoverCanonicalStateTestTypes.generated.h"

/** Custom cover impl that deliberately makes no exact-state coverage claim. */
UCLASS()
class USeinUnclaimedCoverTestSystem : public USeinCoverSystem
{
	GENERATED_BODY()

public:
	virtual void RegisterProvider(
		FSeinEntityHandle /*ProviderHandle*/) override
	{
	}

	virtual void UnregisterProvider(
		FSeinEntityHandle /*ProviderHandle*/) override
	{
	}
};

/** Native default subclass used to prove inherited claims fail closed. */
UCLASS()
class USeinInheritedCoverDefaultTestSystem : public USeinCoverDefault
{
	GENERATED_BODY()
};

/** Native subclass that explicitly claims the inherited shipped-default contract. */
UCLASS()
class USeinClaimedCoverDefaultTestSystem : public USeinCoverDefault
{
	GENERATED_BODY()

public:
	virtual bool ComputeStateCoverageClaim(
		FSeinCoverStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		return ComputeCoverDefaultStateCoverageClaim(
			OutClaim, OutError);
	}
};

/** Stateless custom cover impl that explicitly covers its mutable state. */
UCLASS()
class USeinStatelessClaimedCoverTestSystem : public USeinCoverSystem
{
	GENERATED_BODY()

public:
	virtual void RegisterProvider(
		FSeinEntityHandle /*ProviderHandle*/) override
	{
	}

	virtual void UnregisterProvider(
		FSeinEntityHandle /*ProviderHandle*/) override
	{
	}

	virtual bool ComputeStateCoverageClaim(
		FSeinCoverStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		OutClaim = {};
		OutError.Reset();
		OutClaim.StableImplementationId =
			TEXT("seinarts.tests.cover.stateless");
		OutClaim.BehaviorRevision = 1;
		OutClaim.CoverageRevision = 1;
		OutClaim.StateCoverage =
			ESeinCoverStateCoverage::Stateless;
		return true;
	}
};
