/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandAuthorityPolicy.cpp
 */

#include "Input/SeinCommandAuthorityPolicy.h"

#include "Lib/SeinEntityControlBPFL.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

int32 USeinCommandAuthorityView::GetSimulationTick() const
{
	return World ? World->GetCurrentTick() : 0;
}

ESeinMatchState USeinCommandAuthorityView::GetMatchState() const
{
	return World ? World->GetMatchState() : ESeinMatchState::Lobby;
}

FSeinMatchSettings USeinCommandAuthorityView::GetMatchSettings() const
{
	return World ? World->GetMatchSettings() : FSeinMatchSettings();
}

bool USeinCommandAuthorityView::GetPlayerState(
	FSeinPlayerID Player, FSeinPlayerState& OutState) const
{
	return World && World->GetPlayerStateCopy(Player, OutState);
}

TArray<FSeinPlayerID> USeinCommandAuthorityView::GetRegisteredPlayers() const
{
	return World ? World->GetRegisteredPlayerIDs() : TArray<FSeinPlayerID>();
}

bool USeinCommandAuthorityView::IsEntityValid(FSeinEntityHandle Entity) const
{
	return World && World->GetEntityPool().IsValid(Entity);
}

FSeinPlayerID USeinCommandAuthorityView::GetEntityOwner(
	FSeinEntityHandle Entity) const
{
	return IsEntityValid(Entity)
		? World->GetEntityOwner(Entity)
		: FSeinPlayerID::Neutral();
}

FGameplayTagContainer USeinCommandAuthorityView::GetEntityTags(
	FSeinEntityHandle Entity) const
{
	return IsEntityValid(Entity)
		? World->GetEntityTags(Entity)
		: FGameplayTagContainer();
}

bool USeinCommandAuthorityView::GetEntityComponent(
	FSeinEntityHandle Entity,
	UScriptStruct* ComponentType,
	FInstancedStruct& OutComponent) const
{
	OutComponent.Reset();
	if (!IsEntityValid(Entity) || !ComponentType) return false;
	const ISeinComponentStorage* Storage =
		World->GetComponentStorageRaw(ComponentType);
	const void* Raw = Storage ? Storage->GetComponentRaw(Entity) : nullptr;
	if (!Raw) return false;
	OutComponent.InitializeAs(ComponentType, static_cast<const uint8*>(Raw));
	return true;
}

bool USeinCommandAuthorityView::CanPlayerControlEntity(
	FSeinPlayerID Player,
	FSeinEntityHandle Entity,
	FGameplayTag CommandType) const
{
	return World && USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
		*World, Player, Entity, CommandType, World->GetCurrentTick());
}

bool USeinCommandAuthorityPolicy::AuthorizeCommand_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	ESeinCommandAuthorityScope Scope,
	FGameplayTag& OutRejectionReason) const
{
	OutRejectionReason = SeinARTSTags::Command_Reject_Unauthorized;
	return false;
}

bool USeinCommandAuthorityPolicy::CanControlEntity_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	return false;
}

FSeinPlayerID USeinCommandAuthorityPolicy::ResolveResourcePayer_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	return FSeinPlayerID::Neutral();
}

bool USeinDefaultCommandAuthorityPolicy::AuthorizeCommand_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	ESeinCommandAuthorityScope Scope,
	FGameplayTag& OutRejectionReason) const
{
	OutRejectionReason = SeinARTSTags::Command_Reject_Unauthorized;
	if (!View || Command.IssuerKind == ESeinCommandIssuerKind::Unauthenticated)
	{
		return false;
	}
	const auto IsRegisteredPlayer = [View](FSeinPlayerID Player)
	{
		FSeinPlayerState State;
		return Player.IsValid() && View->GetPlayerState(Player, State);
	};

	const bool bDeterministicSystem =
		Command.IssuerKind == ESeinCommandIssuerKind::DeterministicSystem;
	const bool bExternalPlayer =
		Command.IssuerKind == ESeinCommandIssuerKind::Player
		|| Command.IssuerKind == ESeinCommandIssuerKind::MatchAdministrator;

	switch (Scope)
	{
	case ESeinCommandAuthorityScope::PublicObserver:
		return bDeterministicSystem
			|| (bExternalPlayer && IsRegisteredPlayer(Command.PlayerID));

	case ESeinCommandAuthorityScope::Self:
		return bDeterministicSystem
			|| (bExternalPlayer && IsRegisteredPlayer(Command.PlayerID));

	case ESeinCommandAuthorityScope::Entity:
		return CanControlEntity(View, Command, Command.EntityHandle);

	case ESeinCommandAuthorityScope::EntitySet:
		for (const FSeinEntityHandle Entity : Command.EntityList)
		{
			if (CanControlEntity(View, Command, Entity))
			{
				return true;
			}
		}
		return false;

	case ESeinCommandAuthorityScope::MatchControl:
		return bDeterministicSystem
			|| Command.IssuerKind == ESeinCommandIssuerKind::MatchAdministrator;

	case ESeinCommandAuthorityScope::DerivedSystem:
		return bDeterministicSystem;

	default:
		return false;
	}
}

bool USeinDefaultCommandAuthorityPolicy::CanControlEntity_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	if (!View || !View->IsEntityValid(Entity))
	{
		return false;
	}
	if (Command.IssuerKind == ESeinCommandIssuerKind::DeterministicSystem)
	{
		return true;
	}
	if (Command.IssuerKind != ESeinCommandIssuerKind::Player
		&& Command.IssuerKind != ESeinCommandIssuerKind::MatchAdministrator)
	{
		return false;
	}
	return View->CanPlayerControlEntity(
		Command.PlayerID, Entity, Command.CommandType);
}

FSeinPlayerID USeinDefaultCommandAuthorityPolicy::ResolveResourcePayer_Implementation(
	const USeinCommandAuthorityView* View,
	const FSeinCommand& Command,
	FSeinEntityHandle Entity) const
{
	if (View && View->IsEntityValid(Entity))
	{
		return View->GetEntityOwner(Entity);
	}
	return Command.PlayerID;
}
