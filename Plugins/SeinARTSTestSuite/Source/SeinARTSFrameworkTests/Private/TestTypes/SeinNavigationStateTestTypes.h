#pragma once

#include "SeinNavigation.h"
#include "SeinNavigationAStar.h"
#include "SeinNavigationStateTestTypes.generated.h"

/** Stateful custom nav that deliberately makes no static-environment claim. */
UCLASS()
class USeinOpaqueRuntimeNavigationTestDouble : public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }
};

/** Covers static behavior but deliberately omits the exact-state claim. */
UCLASS()
class USeinStaticDigestOnlyNavigationTestDouble
	: public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }

	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		OutError.Reset();
		OutDigest = FGuid(
			0x534E4156u,
			0x53544154u,
			0x49434F4Eu,
			0x4C595445u);
		return true;
	}
};

/** Stateless custom nav that explicitly covers its immutable behavior. */
UCLASS()
class USeinStatelessClaimedNavigationTestDouble : public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }

	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		OutError.Reset();
		OutDigest = FGuid(
			0x534E4156u,
			0x53544154u,
			0x454C4553u,
			0x53544553u);
		return true;
	}

	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		OutClaim = {};
		OutError.Reset();
		OutClaim.StableImplementationId =
			TEXT("seinarts.tests.navigation.stateless");
		OutClaim.BehaviorRevision = 1;
		OutClaim.CoverageRevision = 1;
		OutClaim.StateCoverage =
			ESeinNavigationStateCoverage::Stateless;
		return true;
	}
};

/** Claims exact stateless coverage but violates the digest contract. */
UCLASS()
class USeinInvalidDigestClaimedNavigationTestDouble
	: public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }

	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		OutError.Reset();
		OutDigest.Invalidate();
		return true;
	}

	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		OutClaim = {};
		OutError.Reset();
		OutClaim.StableImplementationId =
			TEXT("seinarts.tests.navigation.invalid-digest");
		OutClaim.BehaviorRevision = 1;
		OutClaim.CoverageRevision = 1;
		OutClaim.StateCoverage =
			ESeinNavigationStateCoverage::Stateless;
		return true;
	}
};

/** Stateful custom nav whose declared provider is absent from the world schema. */
UCLASS()
class USeinMissingSupplementalNavigationTestDouble
	: public USeinNavigation
{
	GENERATED_BODY()

public:
	virtual bool HasRuntimeData() const override { return true; }

	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		OutError.Reset();
		OutDigest = FGuid(
			0x534E4156u,
			0x4D495353u,
			0x494E4750u,
			0x524F5644u);
		return true;
	}

	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		OutClaim = {};
		OutError.Reset();
		OutClaim.StableImplementationId =
			TEXT("seinarts.tests.navigation.missing-supplemental");
		OutClaim.BehaviorRevision = 1;
		OutClaim.CoverageRevision = 1;
		OutClaim.StateCoverage =
			ESeinNavigationStateCoverage::
				CanonicalStateContributors;
		FSeinCanonicalStateKey& Missing =
			OutClaim.RequiredCanonicalStateContributors.
				AddDefaulted_GetRef();
		Missing.StableDomainId =
			TEXT("seinarts.tests.navigation");
		Missing.StableContributorId =
			TEXT("missing-supplemental-state");
		return true;
	}
};

/** Native A* subclass used to prove inherited claims fail closed. */
UCLASS()
class USeinInheritedAStarNavigationTestDouble
	: public USeinNavigationAStar
{
	GENERATED_BODY()
};

/** Native subclass that explicitly claims the inherited shipped-A* contract. */
UCLASS()
class USeinClaimedAStarNavigationTestDouble
	: public USeinNavigationAStar
{
	GENERATED_BODY()

public:
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		return ComputeAStarStaticEnvironmentDigest(
			OutDigest, OutError);
	}

	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		return ComputeAStarStateCoverageClaim(
			OutClaim, OutError);
	}
};
