/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLobbyViewModel.cpp
 */

#include "ViewModel/SeinLobbyViewModel.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "Subsystems/SeinFactionService.h"
#include "Data/SeinMatchSettings.h"
#include "Settings/PluginSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Containers/Ticker.h"

void USeinLobbyViewModel::Initialize(UWorld* InWorld)
{
	if (!InWorld) return;
	CachedWorld = InWorld;

	// Bind to the lobby actor immediately if it's already replicated; otherwise
	// the ticker below picks it up once it arrives.
	if (USeinLobbySubsystem* Lobby = GetLobbySubsystem())
	{
		if (ASeinLobbyState* Actor = Lobby->GetLobbyState())
		{
			BoundActor = Actor;
			LobbyChangedHandle = Actor->OnLobbyStateChanged.AddUObject(this, &USeinLobbyViewModel::HandleLobbyStateChanged);
			RefreshFromActor();
		}
	}

	// Subscribe to the net subsystem's OnLocalSlotChanged. Replaces the
	// pre-3c tick-based poll. Latch the current value so UI shows the right
	// slot immediately if the relay was already bound at Initialize time.
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		NetLocalSlotChangedHandle = Net->OnLocalSlotChanged.AddUObject(this, &USeinLobbyViewModel::HandleNetLocalSlotChanged);
		LocalSlotID = Net->GetLocalPlayerID();
	}

	// Late-bind ticker: if the lobby actor hasn't replicated yet on the
	// client, poll once per second until it arrives, then unbind. Returns
	// `false` from the ticker delegate to stop ticking once bound — no
	// indefinite tick.
	LobbyActorPollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			if (BoundActor.IsValid())
			{
				return false; // already bound
			}
			if (USeinLobbySubsystem* Lobby = GetLobbySubsystem())
			{
				if (ASeinLobbyState* Actor = Lobby->GetLobbyState())
				{
					BoundActor = Actor;
					LobbyChangedHandle = Actor->OnLobbyStateChanged.AddUObject(this, &USeinLobbyViewModel::HandleLobbyStateChanged);
					RefreshFromActor();
					return false; // bound — stop ticking
				}
			}
			return true; // keep waiting
		}), 1.0f);
}

void USeinLobbyViewModel::Shutdown()
{
	if (BoundActor.IsValid() && LobbyChangedHandle.IsValid())
	{
		BoundActor->OnLobbyStateChanged.Remove(LobbyChangedHandle);
	}
	LobbyChangedHandle.Reset();

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		if (NetLocalSlotChangedHandle.IsValid())
		{
			Net->OnLocalSlotChanged.Remove(NetLocalSlotChangedHandle);
		}
	}
	NetLocalSlotChangedHandle.Reset();

	if (LobbyActorPollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LobbyActorPollHandle);
		LobbyActorPollHandle.Reset();
	}

	BoundActor.Reset();
	CachedSlots.Reset();
	LocalSlotID = FSeinPlayerID::Neutral();
	CachedWorld.Reset();
}

bool USeinLobbyViewModel::TryGetSlot(int32 SlotIndex, FSeinLobbySlotState& OutSlot) const
{
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.SlotIndex == SlotIndex)
		{
			OutSlot = Slot;
			return true;
		}
	}
	return false;
}

bool USeinLobbyViewModel::IsHost() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return false;
	const ENetMode Mode = World->GetNetMode();
	return Mode == NM_ListenServer || Mode == NM_DedicatedServer || Mode == NM_Standalone;
}

bool USeinLobbyViewModel::IsInLobbySession() const
{
	// Two conditions:
	//   1. Networking is active (NetMode != Standalone) — Standalone is
	//      single-player; the lobby has no real meaning there.
	//   2. The local PC has a slot assigned (relay registered + latched).
	const UWorld* World = CachedWorld.Get();
	if (!World) return false;
	if (World->GetNetMode() == NM_Standalone) return false;
	return !GetLocalSlot().IsNeutral();
}

bool USeinLobbyViewModel::CanStartMatch() const
{
	if (!IsHost()) return false;

	// At least one Human-claimed slot. Designer's host UI can layer
	// additional gates (e.g. "all ready required") by reading per-slot
	// `bReady` from `GetSlots()` and disabling the Start Match button
	// in BP — framework permits Start whenever there's a live human.
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.bClaimed && Slot.State == ESeinSlotState::Human && !Slot.bDisconnected)
		{
			return true;
		}
	}
	return false;
}

bool USeinLobbyViewModel::HasPublishedMatchSnapshot() const
{
	const USeinLobbySubsystem* Lobby = GetLobbySubsystem();
	return Lobby && Lobby->HasPublishedSnapshot();
}

FSeinPlayerID USeinLobbyViewModel::GetLocalSlot() const
{
	// Read live from NetSubsystem rather than cached LocalSlotID. Source of
	// truth for "what slot is this PC bound to" is `USeinNetSubsystem::LocalPlayerID`.
	// Try the cached world first, then fall back to the outer chain in case
	// CachedWorld got reset by a Shutdown call but the BP still holds a ref.
	UWorld* ResolvedWorld = CachedWorld.Get();
	if (!ResolvedWorld)
	{
		if (UObject* Outer = GetOuter())
		{
			ResolvedWorld = Outer->GetWorld();
		}
	}
	if (ResolvedWorld)
	{
		if (UGameInstance* GI = ResolvedWorld->GetGameInstance())
		{
			if (USeinNetSubsystem* Net = GI->GetSubsystem<USeinNetSubsystem>())
			{
				const FSeinPlayerID Live = Net->GetLocalPlayerID();
				// Verbose: BlueprintPure + UMG bindings call this every frame;
				// noise at Log level. Re-enable with `LogTemp Verbose` when
				// diagnosing slot-binding races.
				UE_LOG(LogTemp, Verbose, TEXT("[VM] GetLocalSlot via Net: %u  (cached=%u, world=%s)"),
					Live.Value, LocalSlotID.Value, *GetNameSafe(ResolvedWorld));
				return Live;
			}
		}
	}
	// Fall-through path is unusual (Net subsystem missing — usually a teardown
	// race). Verbose so the per-frame UMG re-evaluation doesn't drown the log;
	// promote to Warning if you're chasing a "stuck Neutral" bug.
	UE_LOG(LogTemp, Verbose, TEXT("[VM] GetLocalSlot: Net unreachable (CachedWorld=%s), falling back to cached %u"),
		CachedWorld.IsValid() ? TEXT("valid") : TEXT("null"),
		LocalSlotID.Value);
	return LocalSlotID;
}

bool USeinLobbyViewModel::IsLocalSlot(int32 SlotIndex) const
{
	if (SlotIndex <= 0) return false;
	const FSeinPlayerID Local = GetLocalSlot();
	if (Local.IsNeutral()) return false;
	return static_cast<int32>(Local.Value) == SlotIndex;
}

bool USeinLobbyViewModel::IsLocalReady() const
{
	const FSeinPlayerID LocalSlot = GetLocalSlot();
	if (LocalSlot.IsNeutral()) return false;
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.SlotIndex == (int32)LocalSlot.Value)
		{
			return Slot.bReady;
		}
	}
	return false;
}

int32 USeinLobbyViewModel::GetReadyCount() const
{
	// Counts BOTH Human (toggled-ready) AND AI (implicitly ready — server
	// stamps bReady=true on AI slots in ServerHandleSetSlotState). Without
	// counting AI here, "Ready: 0/2" would display in a 1-Human + 1-AI lobby
	// where the human hasn't toggled ready, even though the AI IS ready
	// from the framework's perspective.
	int32 Count = 0;
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (!Slot.bClaimed) continue;
		const bool bIsCountable =
			(Slot.State == ESeinSlotState::Human && Slot.bReady) ||
			(Slot.State == ESeinSlotState::AI);
		if (bIsCountable) ++Count;
	}
	return Count;
}

int32 USeinLobbyViewModel::GetClaimedCount() const
{
	// Counts both Human and AI claimed slots, so the "X/Y ready" display
	// has the same denominator as the Ready count's numerator.
	int32 Count = 0;
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (!Slot.bClaimed) continue;
		if (Slot.State == ESeinSlotState::Human || Slot.State == ESeinSlotState::AI)
		{
			++Count;
		}
	}
	return Count;
}

TArray<TSoftObjectPtr<USeinFaction>> USeinLobbyViewModel::GetAvailableFactionsForPicker() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return {};
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return {};
	USeinFactionService* FS = GI->GetSubsystem<USeinFactionService>();
	if (!FS) return {};

	return FS->GetAvailableFactions();
}

TSoftObjectPtr<UWorld> USeinLobbyViewModel::GetSelectedMap() const
{
	if (const ASeinLobbyState* Actor = BoundActor.Get())
	{
		return Actor->SelectedMap;
	}
	return TSoftObjectPtr<UWorld>();
}

TArray<FSeinLobbyMapEntry> USeinLobbyViewModel::GetAvailableMaps() const
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings ? Settings->AvailableMaps : TArray<FSeinLobbyMapEntry>{};
}

bool USeinLobbyViewModel::IsMapShrinkSafe(const FSeinLobbyMapEntry& Candidate) const
{
	// Walk the cached slot array — any claimed slot whose SlotIndex exceeds
	// Candidate.SlotCount would be lost on resize, so the switch isn't safe.
	// Mirrors the reject policy in `USeinLobbySubsystem::ServerHandleSelectMap`.
	const int32 NewCount = FMath::Max(0, Candidate.SlotCount);
	for (const FSeinLobbySlotState& Slot : CachedSlots)
	{
		if (Slot.bClaimed && Slot.SlotIndex > NewCount)
		{
			return false;
		}
	}
	return true;
}

bool USeinLobbyViewModel::TryGetCurrentMapEntry(FSeinLobbyMapEntry& OutEntry) const
{
	const TSoftObjectPtr<UWorld> Selected = GetSelectedMap();
	if (Selected.IsNull()) return false;

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return false;

	const FSoftObjectPath SelectedPath = Selected.ToSoftObjectPath();
	for (const FSeinLobbyMapEntry& Entry : Settings->AvailableMaps)
	{
		if (Entry.Map.ToSoftObjectPath() == SelectedPath)
		{
			OutEntry = Entry;
			return true;
		}
	}
	return false;
}

int32 USeinLobbyViewModel::GetCurrentMapTeamCount() const
{
	FSeinLobbyMapEntry Entry;
	if (TryGetCurrentMapEntry(Entry))
	{
		return Entry.TeamCount;
	}
	return 2; // 1v1 default — matches FSeinLobbyMapEntry::TeamCount default.
}

USeinLobbySubsystem* USeinLobbyViewModel::GetLobbySubsystem() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<USeinLobbySubsystem>() : nullptr;
}

USeinNetSubsystem* USeinLobbyViewModel::GetNetSubsystem() const
{
	const UWorld* World = CachedWorld.Get();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<USeinNetSubsystem>() : nullptr;
}

void USeinLobbyViewModel::RefreshFromActor()
{
	const ASeinLobbyState* Actor = BoundActor.Get();
	if (!Actor)
	{
		CachedSlots.Reset();
	}
	else
	{
		CachedSlots = Actor->Slots;
	}
	OnLobbyChanged.Broadcast();
}

void USeinLobbyViewModel::HandleLobbyStateChanged()
{
	// Skip refreshes during world teardown. Without this, OnLogout firing on
	// host-click triggers RefreshFromActor → BP RefreshSlotPanel → child-widget
	// spawn, which fails an engine ensure (UMG forbids widget creation on a
	// world that's BeginTearingDown). Cosmetic during shutdown, but the
	// ensure spam masks real errors.
	const UWorld* World = CachedWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	RefreshFromActor();
}

void USeinLobbyViewModel::HandleNetLocalSlotChanged(FSeinPlayerID NewSlot)
{
	if (LocalSlotID == NewSlot) return;
	LocalSlotID = NewSlot;
	OnLocalSlotChanged.Broadcast(LocalSlotID);
}
