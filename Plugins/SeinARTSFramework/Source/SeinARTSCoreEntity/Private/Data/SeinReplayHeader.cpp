/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayHeader.cpp
 */

#include "Data/SeinReplayHeader.h"
#include "UObject/Package.h"

#include "Engine/World.h"
#include "Misc/NetworkVersion.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	// Manual compatibility epoch for deterministic framework behaviour that is
	// not already represented by the command/config/settings digests.
	constexpr TCHAR GSeinReplayFrameworkVersion[] = TEXT("SeinARTS.Replay.5");
}

FString SeinReplayCompatibility::GetFrameworkVersion()
{
	return GSeinReplayFrameworkVersion;
}

FString SeinReplayCompatibility::GetGameVersion()
{
	return FString::Printf(
		TEXT("UE.Network.%08X"),
		FNetworkVersion::GetLocalNetworkVersion());
}

FName SeinReplayCompatibility::GetMapIdentifier(const UWorld* World)
{
	if (!World || !World->GetOutermost()) return NAME_None;
	return FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
}

void SeinReplayCompatibility::StampCurrent(
	FSeinReplayHeader& Header,
	const UWorld* World)
{
	Header.FrameworkVersion = GetFrameworkVersion();
	Header.GameVersion = GetGameVersion();
	const FName CurrentMap = GetMapIdentifier(World);
	Header.MapIdentifier = CurrentMap.IsNone()
		? FString()
		: CurrentMap.ToString();
}

bool SeinReplayCompatibility::ValidateCurrent(
	const FSeinReplayHeader& Header,
	const UWorld* World,
	FString& OutError)
{
	OutError.Reset();
	const FString CurrentFramework = GetFrameworkVersion();
	if (Header.FrameworkVersion != CurrentFramework)
	{
		OutError = FString::Printf(
			TEXT("framework compatibility mismatch (file=%s local=%s)"),
			*Header.FrameworkVersion,
			*CurrentFramework);
		return false;
	}

	const FString CurrentGame = GetGameVersion();
	if (Header.GameVersion != CurrentGame)
	{
		OutError = FString::Printf(
			TEXT("game compatibility mismatch (file=%s local=%s)"),
			*Header.GameVersion,
			*CurrentGame);
		return false;
	}

	const FName CurrentMap = GetMapIdentifier(World);
	if (CurrentMap.IsNone())
	{
		OutError = TEXT("current world has no canonical long-package map identity");
		return false;
	}
	if (Header.MapIdentifier != CurrentMap.ToString())
	{
		OutError = FString::Printf(
			TEXT("map compatibility mismatch (file=%s local=%s)"),
			*Header.MapIdentifier,
			*CurrentMap.ToString());
		return false;
	}
	const USeinWorldSubsystem* WorldSub =
		World->GetSubsystem<USeinWorldSubsystem>();
	if (!WorldSub || !WorldSub->IsSimulationContentReady()
		|| !Header.BootstrapReceipt.IsValid()
		|| Header.BootstrapReceipt.SimulationContentDigest
			!= WorldSub->GetSimulationContentDigest())
	{
		OutError =
			TEXT("simulation-content compatibility mismatch or unavailable local manifest");
		return false;
	}
	return true;
}

bool SeinReplayCompatibility::ValidatePlayerManifest(
	const FSeinReplayHeader& Header,
	FString& OutError)
{
	OutError.Reset();
	TMap<uint8, const FSeinMatchSlot*> ActiveSlots;
	for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
	{
		if (Slot.State != ESeinSlotState::Human
			&& Slot.State != ESeinSlotState::AI)
		{
			continue;
		}
		if (Slot.SlotIndex <= 0 || Slot.SlotIndex > MAX_uint8
			|| ActiveSlots.Contains(static_cast<uint8>(Slot.SlotIndex)))
		{
			OutError = TEXT("replay active-slot manifest is invalid or ambiguous");
			return false;
		}
		ActiveSlots.Add(static_cast<uint8>(Slot.SlotIndex), &Slot);
	}

	TSet<uint8> RegisteredActiveSlots;
	uint8 PreviousPlayerID = 0;
	for (const FSeinPlayerRegistration& Player : Header.Players)
	{
		const uint8 PlayerID = Player.PlayerID.Value;
		if (!Player.PlayerID.IsValid() || PlayerID <= PreviousPlayerID)
		{
			OutError = TEXT("replay player registrations must have unique ascending non-neutral IDs");
			return false;
		}
		PreviousPlayerID = PlayerID;
		const FSeinMatchSlot* const* Slot = ActiveSlots.Find(PlayerID);
		if (Player.bIsSpectator)
		{
			if (Slot || Player.bIsAI || Player.FactionID.IsValid()
				|| Player.TeamID != 0)
			{
				OutError = TEXT("replay spectator registration owns gameplay identity or team state");
				return false;
			}
			continue;
		}
		if (!Slot
			|| Player.bIsAI != ((*Slot)->State == ESeinSlotState::AI)
			|| Player.FactionID != (*Slot)->FactionID
			|| Player.TeamID != (*Slot)->TeamID)
		{
			OutError = TEXT("replay player registration disagrees with its active match slot");
			return false;
		}
		RegisteredActiveSlots.Add(PlayerID);
	}

	if (RegisteredActiveSlots.Num() != ActiveSlots.Num())
	{
		OutError = TEXT("replay player registrations do not cover every active match slot");
		return false;
	}
	return true;
}
