#pragma once

#include "Collision/SeinCollisionResolver.h"
#include "Collision/SeinCollisionResolverDefault.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "SeinCollisionStateTestTypes.generated.h"

/** Custom resolver that deliberately makes no exact-state claim. */
UCLASS()
class USeinUnclaimedCollisionResolverTestDouble
	: public USeinCollisionResolver
{
	GENERATED_BODY()
};

/** Native subclass of the shipped default that re-declares its own coverage
 *  and config digest, proving the opt-in subclass path stays open. */
UCLASS()
class USeinClaimedCollisionResolverTestDouble
	: public USeinCollisionResolverDefault
{
	GENERATED_BODY()

public:
	virtual bool ComputeStateCoverageClaim(
		FSeinCollisionResolverStateCoverageClaim& OutClaim,
		FString& OutError) const override
	{
		if (!ComputeDefaultResolverStateCoverageClaim(OutClaim, OutError))
		{
			return false;
		}
		OutClaim.StableImplementationId =
			TEXT("seinarts.tests.collision.claimed");
		return true;
	}

	virtual bool ComputeResolutionConfigDigest(
		FGuid& OutDigest,
		FString& OutError) const override
	{
		OutDigest.Invalidate();
		OutError.Reset();
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTSTests.Collision.Claimed.ResolutionConfig"), 1);
		if (!Writer.WriteString(GetClass()->GetPathName()))
		{
			OutError = Writer.GetError();
			return false;
		}
		return Writer.Finalize(OutDigest, OutError);
	}
};
