/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyState.cpp
 */

#include "SeinLobbyState.h"
#include "SeinARTSNet.h"
#include "SeinLobbySubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"

ASeinLobbyState::ASeinLobbyState()
{
	bReplicates = true;
	bAlwaysRelevant = true; // every client must see the lobby slot array
	bNetLoadOnClient = false; // dynamic spawn only (server creates at first PostLogin)

	PrimaryActorTick.bCanEverTick = false;
	SetActorTickEnabled(false);
}

void ASeinLobbyState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASeinLobbyState, Slots);
	DOREPLIFETIME(ASeinLobbyState, SelectedMap);
}

void ASeinLobbyState::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogSeinNet, Verbose,
		TEXT("ASeinLobbyState::BeginPlay  NetMode=%d  LocalRole=%d  Slots=%d"),
		(int32)GetNetMode(), (int32)GetLocalRole(), Slots.Num());

	// Notify the GI-level subsystem so it can latch a back-reference (the
	// subsystem outlives the actor across map travel; the actor is per-world).
	if (USeinLobbySubsystem* Sub = GetLobbySubsystem())
	{
		Sub->NotifyLobbyStateActorBeginPlay(this);
	}
}

void ASeinLobbyState::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USeinLobbySubsystem* Sub = GetLobbySubsystem())
	{
		Sub->NotifyLobbyStateActorEndPlay(this);
	}
	Super::EndPlay(Reason);
}

void ASeinLobbyState::OnRep_Slots()
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] OnRep_Slots: %d slot(s) replicated."), Slots.Num());
	OnLobbyStateChanged.Broadcast();
}

void ASeinLobbyState::OnRep_SelectedMap()
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] OnRep_SelectedMap: '%s'"),
		*SelectedMap.ToSoftObjectPath().ToString());
	// Same broadcast channel as slot updates — UI lobby refresh handles
	// map-driven changes (slot panel resize, map thumbnail, etc.).
	OnLobbyStateChanged.Broadcast();
}

const FSeinLobbySlotState* ASeinLobbyState::FindSlot(int32 SlotIndex) const
{
	for (const FSeinLobbySlotState& S : Slots)
	{
		if (S.SlotIndex == SlotIndex) return &S;
	}
	return nullptr;
}

FSeinLobbySlotState* ASeinLobbyState::FindSlotMutable(int32 SlotIndex)
{
	for (FSeinLobbySlotState& S : Slots)
	{
		if (S.SlotIndex == SlotIndex) return &S;
	}
	return nullptr;
}

USeinLobbySubsystem* ASeinLobbyState::GetLobbySubsystem() const
{
	const UWorld* World = GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<USeinLobbySubsystem>();
}
