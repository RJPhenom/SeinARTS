#include "Simulation/SeinTestMatchBootstrap.h"

#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinTestBootstrap, Log, All);

namespace
{
	constexpr const TCHAR* TestBootstrapAuthorityID =
		TEXT("SeinARTSTestSupport");

	bool Fail(const FString& Error, FString* OutError)
	{
		if (OutError)
		{
			*OutError = Error;
		}
		UE_LOG(LogSeinTestBootstrap, Error, TEXT("%s"), *Error);
		return false;
	}

	bool ComputeContextDigest(
		const FGuid& ContractDigest,
		int64 SessionSeed,
		FName FixtureID,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.TestBootstrap.Context"), 1);
		Writer.WriteGuid(ContractDigest);
		Writer.WriteInt64(SessionSeed);
		Writer.WriteString(
			FSeinCanonicalInitialStateDigest::CanonicalContributorID(FixtureID));
		return Writer.Finalize(OutDigest, OutError);
	}

	bool ComputePlanDigest(
		const USeinWorldSubsystem& World,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.TestBootstrap.Plan"), 1);
		Writer.WriteGuid(World.GetMatchBootstrapAuthorizationContextDigest());
		return Writer.Finalize(OutDigest, OutError);
	}

	bool ClaimAuthority(
		USeinWorldSubsystem& World,
		FSeinMatchBootstrapAuthorityHandle& OutAuthority,
		FString& OutError)
	{
		return World.ClaimMatchBootstrapAuthority(
			TestBootstrapAuthorityID,
			&World,
			OutAuthority,
			OutError);
	}
}

bool SeinTestMatchBootstrap::Materialize(
	USeinWorldSubsystem& World,
	const FSeinMatchSettings& Settings,
	int64 SessionSeed,
	FName FixtureID,
	FString* OutError)

{
	const auto AuthorNothing = []() {};
	return Materialize(
		World, AuthorNothing, Settings, SessionSeed, FixtureID, OutError);
}

bool SeinTestMatchBootstrap::Materialize(
	USeinWorldSubsystem& World,
	TFunctionRef<void()> AuthorState,
	const FSeinMatchSettings& Settings,
	int64 SessionSeed,
	FName FixtureID,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	if (FixtureID.IsNone())
	{
		return Fail(TEXT("Test bootstrap requires a stable fixture ID."), OutError);
	}

	FSeinMatchBootstrapAuthorityHandle Authority;
	FString Error;
	if (!ClaimAuthority(World, Authority, Error))
	{
		return Fail(Error, OutError);
	}

	FSeinMatchSettings CanonicalSettings = Settings;
	FGuid ContractDigest;
	if (!SeinCanonicalizeAndDigestMatchSettings(
			CanonicalSettings, ContractDigest, nullptr))
	{
		return Fail(TEXT("Test bootstrap could not digest its match settings."), OutError);
	}
	FGuid ContextDigest;
	if (!ComputeContextDigest(
			ContractDigest, SessionSeed, FixtureID, ContextDigest, Error))
	{
		return Fail(Error, OutError);
	}

	if (World.GetMatchBootstrapState() == ESeinMatchBootstrapState::Awaiting)
	{
		if (!World.SeedSimRandom(Authority, SessionSeed, Error))
		{
			return Fail(Error, OutError);
		}
	}

	const FSeinMatchBootstrapMaterializer PreviousMaterializer =
		World.MatchBootstrapMaterializer;
	World.MatchBootstrapMaterializer.BindLambda(
		[&World, &AuthorState, ContractDigest](
			const FSeinMatchSettings& MaterializedSettings,
			const FGuid& MaterializedContext,
			FSeinMatchBootstrapReceipt& OutReceipt,
			FString& OutMaterializerError)
		{
			if (World.GetMatchBootstrapState()
					!= ESeinMatchBootstrapState::Applying
				|| World.GetMatchBootstrapAuthorizationContextDigest()
					!= MaterializedContext)
			{
				OutMaterializerError =
					TEXT("Core did not open the test materializer's canonical bootstrap transaction.");
				return false;
			}

			World.StartMatch(MaterializedSettings);
			if (World.GetMatchBootstrapState()
					!= ESeinMatchBootstrapState::Applying
				|| World.GetMatchState() != ESeinMatchState::Starting
				|| World.GetMatchSettingsDigest() != ContractDigest)
			{
				OutMaterializerError = TEXT(
					"Test bootstrap could not install its canonical match contract.");
				return false;
			}

			AuthorState();
			if (World.GetMatchBootstrapState()
				!= ESeinMatchBootstrapState::Applying)
			{
				OutMaterializerError = TEXT(
					"Test bootstrap authoring did not preserve Applying state.");
				return false;
			}

			FGuid PlanDigest;
			return ComputePlanDigest(
				World, PlanDigest, OutMaterializerError)
				&& World.SealLocalMatchBootstrap(
					PlanDigest, OutReceipt, OutMaterializerError);
		});

	FSeinMatchBootstrapReceipt Receipt;
	const bool bReady = World.EnsureMatchBootstrapLocallyReady(
		Authority,
		CanonicalSettings,
		ContextDigest,
		Receipt,
		Error);
	World.MatchBootstrapMaterializer = PreviousMaterializer;
	return bReady ? true : Fail(Error, OutError);
}

bool SeinTestMatchBootstrap::Authorize(
	USeinWorldSubsystem& World,
	FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}
	FSeinMatchBootstrapAuthorityHandle Authority;
	FString Error;
	if (!ClaimAuthority(World, Authority, Error))
	{
		return Fail(Error, OutError);
	}
	const ESeinMatchBootstrapState State = World.GetMatchBootstrapState();
	if (State == ESeinMatchBootstrapState::Authorized
		|| State == ESeinMatchBootstrapState::Consumed)
	{
		return true;
	}
	if (State != ESeinMatchBootstrapState::LocallyReady)
	{
		return Fail(
			TEXT("Test bootstrap cannot authorize from the current state."),
			OutError);
	}

	FSeinMatchBootstrapReceipt Receipt = World.GetMatchBootstrapReceipt();
	if (!World.AuthorizeMatchBootstrap(
			Authority,
			Receipt,
			World.GetMatchBootstrapAuthorizationContextDigest(),
			Error))
	{
		return Fail(Error, OutError);
	}
	return true;
}

bool SeinTestMatchBootstrap::Start(
	USeinWorldSubsystem& World,
	FString* OutError)
{
	if (World.GetMatchBootstrapState() == ESeinMatchBootstrapState::Awaiting
		&& !Materialize(World, FSeinMatchSettings(), 0,
			FName(TEXT("SeinARTSTestSupport.Start")), OutError))
	{
		return false;
	}
	if (!Authorize(World, OutError))
	{
		return false;
	}

	if (World.GetMatchBootstrapState() == ESeinMatchBootstrapState::Consumed)
	{
		if (!World.StartSimulation())
		{
			return Fail(
				TEXT("Test bootstrap simulation resume was refused."), OutError);
		}
		return true;
	}

	FSeinMatchBootstrapAuthorityHandle Authority;
	FString Error;
	if (!ClaimAuthority(World, Authority, Error)
		|| !World.LaunchAuthorizedMatchBootstrap(Authority, Error))
	{
		return Fail(Error, OutError);
	}
	return true;
}
