/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapSubsystem.cpp
 */

#include "GameMode/SeinMatchBootstrapSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameMode/SeinMatchBootstrapTransaction.h"
#include "GameMode/SeinPlayerStart.h"
#include "GameMode/SeinWorldSettings.h"
#include "SeinLobbySubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const FName StandaloneBootstrapAuthorityID(
		TEXT("SeinARTS.Framework.StandaloneBootstrap"));

	bool ResolveDirectMatchSettings(
		UWorld& World, FSeinMatchSettings& OutSettings, bool bAllowLobbyContract)
	{
		if (UGameInstance* GameInstance = bAllowLobbyContract
			? World.GetGameInstance()
			: nullptr)
		{
			if (USeinLobbySubsystem* Lobby =
				GameInstance->GetSubsystem<USeinLobbySubsystem>())
			{
				if (Lobby->HasPublishedSnapshot()
					&& !Lobby->GetPublishedSnapshot().Slots.IsEmpty())
				{
					OutSettings = Lobby->GetPublishedSnapshot();
					return true;
				}
			}
		}

		OutSettings = ASeinPlayerStart::SynthesizeMatchSettingsFromLevel(&World);
		return !OutSettings.Slots.IsEmpty();
	}

	bool HasExternalBootstrapIntent(const UWorld& World)
	{
		if (const ASeinWorldSettings* Settings =
			Cast<ASeinWorldSettings>(World.GetWorldSettings()))
		{
			if (Settings->BootstrapIntent
				== ESeinWorldBootstrapIntent::ExternalOrchestrator)
			{
				return true;
			}
		}

		const TCHAR* IntentOption = World.URL.GetOption(
			TEXT("SeinBootstrap="), nullptr);
		return IntentOption
			&& FCString::Stricmp(
				IntentOption, TEXT("ExternalOrchestrator")) == 0;
	}

	bool IsExplicitStandaloneLaunch(const UWorld& World)
	{
		const TCHAR* IntentOption = World.URL.GetOption(
			TEXT("SeinBootstrap="), nullptr);
		return IntentOption
			&& FCString::Stricmp(
				IntentOption, TEXT("StandaloneLaunch")) == 0;
	}

	bool ShouldAutoStart(const UWorld& World)
	{
		if (IsExplicitStandaloneLaunch(World))
		{
			// An explicit lobby start remains explicit across standalone travel,
			// even when the destination level disables ambient auto-start.
			return true;
		}
		const ASeinWorldSettings* Settings =
			Cast<ASeinWorldSettings>(World.GetWorldSettings());
		return !Settings || Settings->bAutoStartSim;
	}
}

USeinMatchBootstrapSubsystem::~USeinMatchBootstrapSubsystem() = default;

void USeinMatchBootstrapSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());
	Collection.InitializeDependency(USeinActorBridgeSubsystem::StaticClass());

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSubsystem =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	USeinActorBridgeSubsystem* ActorBridge =
		World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr;
	if (!WorldSubsystem || !ActorBridge)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: required world subsystems are unavailable."));
		return;
	}

	if (WorldSubsystem->MatchBootstrapMaterializer.IsBound()
		|| WorldSubsystem->StandaloneBootstrapLauncher.IsBound())
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: a materializer or standalone launcher was already bound; the default Framework facade will not replace it."));
		return;
	}

	BoundWorldSubsystem = WorldSubsystem;
	WorldSubsystem->MatchBootstrapMaterializer.BindUObject(
		this, &USeinMatchBootstrapSubsystem::MaterializeMatchBootstrap);
	WorldSubsystem->StandaloneBootstrapLauncher.BindUObject(
		this, &USeinMatchBootstrapSubsystem::LaunchStandaloneSimulation);
	BootstrapClosedHandle = WorldSubsystem->OnMatchBootstrapClosed.AddUObject(
		this, &USeinMatchBootstrapSubsystem::HandleMatchBootstrapClosed);

	// The transaction owns the complete placed-actor order.
	ActorBridge->SetAutoRegisterOnBeginPlay(false);
}

void USeinMatchBootstrapSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinMatchBootstrapSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	if (USeinWorldSubsystem* WorldSubsystem = BoundWorldSubsystem.Get())
	{
		if (WorldSubsystem->MatchBootstrapMaterializer.IsBoundToObject(this))
		{
			WorldSubsystem->MatchBootstrapMaterializer.Unbind();
		}
		if (WorldSubsystem->StandaloneBootstrapLauncher.IsBoundToObject(this))
		{
			WorldSubsystem->StandaloneBootstrapLauncher.Unbind();
		}
		if (BootstrapClosedHandle.IsValid())
		{
			WorldSubsystem->OnMatchBootstrapClosed.Remove(BootstrapClosedHandle);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (USeinActorBridgeSubsystem* ActorBridge =
			World->GetSubsystem<USeinActorBridgeSubsystem>())
		{
			// This facade no longer owns the bootstrap order. Re-enable the
			// bridge's ordinary fallback for any future BeginPlay.
			ActorBridge->SetAutoRegisterOnBeginPlay(true);
		}
	}

	BootstrapClosedHandle.Reset();
	BootstrapAuthority = FSeinMatchBootstrapAuthorityHandle();
	BoundWorldSubsystem.Reset();
	ReleaseTransaction();
}

void USeinMatchBootstrapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* WorldSubsystem = BoundWorldSubsystem.Get();
	if (!WorldSubsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: Core world subsystem binding was lost before BeginPlay."));
		return;
	}

	if (InWorld.GetNetMode() != NM_Standalone)
	{
		// Framework cannot infer whether this process is a simulation participant
		// (a dedicated coordinator need not be one). Net's participant-aware
		// receipt path installs the seed and calls Ensure only on sim peers.
		UE_LOG(LogTemp, Log,
			TEXT("SeinMatchBootstrap: network world is awaiting its topology adapter."));
		return;
	}

	if (HasExternalBootstrapIntent(InWorld))
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinMatchBootstrap: standalone world is reserved for an external bootstrap orchestrator."));
		return;
	}

	if (ShouldAutoStart(InWorld))
	{
		// Only a lobby-launched world may consume the lobby's published
		// contract here; an ambient auto-start anywhere else (the menu map a
		// kicked client returns to, a directly opened level) synthesizes its
		// contract from the level alone.
		if (EnsureStandaloneBootstrapAuthorized(
				/*bAllowLobbyContract=*/IsExplicitStandaloneLaunch(InWorld)))
		{
			LaunchStandaloneSimulation();
		}
	}
}

bool USeinMatchBootstrapSubsystem::EnsureStandaloneBootstrapAuthorized(
	bool bAllowLobbyContract)
{
	USeinWorldSubsystem* WorldSubsystem = BoundWorldSubsystem.Get();
	UWorld* World = GetWorld();
	if (!WorldSubsystem || !World || World->GetNetMode() != NM_Standalone
		|| HasExternalBootstrapIntent(*World))
	{
		return false;
	}

	const ESeinMatchBootstrapState State =
		WorldSubsystem->GetMatchBootstrapState();
	if (State == ESeinMatchBootstrapState::Consumed)
	{
		return true;
	}
	if (State == ESeinMatchBootstrapState::Applying
		|| State == ESeinMatchBootstrapState::Failed)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: standalone preparation cannot continue from bootstrap state %d."),
			static_cast<int32>(State));
		return false;
	}

	FSeinMatchSettings Settings;
	if (!ResolveDirectMatchSettings(*World, Settings, bAllowLobbyContract))
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinMatchBootstrap: no standalone match contract was found; world remains Awaiting."));
		return false;
	}

	FGuid ContextDigest;
	FString Error;
	if (State == ESeinMatchBootstrapState::Awaiting)
	{
		if (!WorldSubsystem->ClaimMatchBootstrapAuthority(
			StandaloneBootstrapAuthorityID, this, BootstrapAuthority, Error))
		{
			UE_LOG(LogTemp, Error,
				TEXT("SeinMatchBootstrap: standalone authority claim failed: %s"),
				*Error);
			return false;
		}
		if (!WorldSubsystem->SeedSimRandom(
				BootstrapAuthority, 0, Error))
		{
			UE_LOG(LogTemp, Error,
				TEXT("SeinMatchBootstrap: standalone session seeding failed: %s"),
				*Error);
			return false;
		}
	}
	if (!FSeinMatchBootstrapTransaction::
		ComputeStandaloneAuthorizationContextDigest(
			*World, *WorldSubsystem, Settings, ContextDigest, Error))
	{
		if (BootstrapAuthority.IsValid())
		{
			FString FailureError;
			WorldSubsystem->FailMatchBootstrap(
				BootstrapAuthority, Error, FailureError);
		}
		return false;
	}

	FSeinMatchBootstrapReceipt Receipt;
	if (!WorldSubsystem->EnsureMatchBootstrapLocallyReady(
		BootstrapAuthority, Settings, ContextDigest, Receipt, Error))
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: standalone materialization failed: %s"),
			*Error);
		return false;
	}
	if (!WorldSubsystem->AuthorizeMatchBootstrap(
		BootstrapAuthority, Receipt, ContextDigest, Error))
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: standalone authorization failed: %s"),
			*Error);
		return false;
	}
	return true;
}

bool USeinMatchBootstrapSubsystem::LaunchStandaloneSimulation()
{
	USeinWorldSubsystem* WorldSubsystem = BoundWorldSubsystem.Get();
	UWorld* World = GetWorld();
	if (!WorldSubsystem || !World || World->GetNetMode() != NM_Standalone)
	{
		return false;
	}
	if (!EnsureStandaloneBootstrapAuthorized())
	{
		return false;
	}

	if (WorldSubsystem->GetMatchBootstrapState()
		== ESeinMatchBootstrapState::Consumed)
	{
		return WorldSubsystem->StartSimulation();
	}

	FString Error;
	const bool bLaunched = WorldSubsystem->LaunchAuthorizedMatchBootstrap(
		BootstrapAuthority, Error);
	if (!bLaunched)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: standalone launch request failed: %s"),
			*Error);
	}
	return bLaunched;
}

bool USeinMatchBootstrapSubsystem::MaterializeMatchBootstrap(
	const FSeinMatchSettings& Settings,
	const FGuid& AuthorizationContextDigest,
	FSeinMatchBootstrapReceipt& OutReceipt,
	FString& OutError)
{
	if (Transaction || bMaterializerExecuting)
	{
		OutError = TEXT("Framework bootstrap materializer was re-entered.");
		return false;
	}

	UWorld* World = GetWorld();
	USeinWorldSubsystem* WorldSubsystem = BoundWorldSubsystem.Get();
	USeinActorBridgeSubsystem* ActorBridge =
		World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr;
	if (!World || !WorldSubsystem || !ActorBridge)
	{
		OutError = TEXT("Framework bootstrap materializer dependencies are unavailable.");
		return false;
	}

	Transaction = MakeUnique<FSeinMatchBootstrapTransaction>(
		*World, *WorldSubsystem, *ActorBridge);
	bMaterializerExecuting = true;
	bReleaseTransactionWhenMaterializerReturns = false;
	const bool bMaterialized = Transaction->Materialize(
		Settings, AuthorizationContextDigest, OutReceipt, OutError);
	bMaterializerExecuting = false;

	if (!bMaterialized || bReleaseTransactionWhenMaterializerReturns)
	{
		ReleaseTransaction();
	}
	return bMaterialized;
}

void USeinMatchBootstrapSubsystem::HandleMatchBootstrapClosed(bool bAuthorized)
{
	if (bAuthorized)
	{
		UE_LOG(LogTemp, Log,
			TEXT("SeinMatchBootstrap: transaction closed (authorized)."));
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("SeinMatchBootstrap: transaction closed (failed)."));
	}
	if (bMaterializerExecuting)
	{
		bReleaseTransactionWhenMaterializerReturns = true;
		return;
	}
	ReleaseTransaction();
}

void USeinMatchBootstrapSubsystem::ReleaseTransaction()
{
	Transaction.Reset();
	bReleaseTransactionWhenMaterializerReturns = false;
}
