/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerStart.cpp
 * @brief   RTS player start implementation.
 */

#include "GameMode/SeinPlayerStart.h"
#include "EngineUtils.h"
#include "Engine/World.h"

ASeinPlayerStart::ASeinPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FSeinMatchSettings ASeinPlayerStart::SynthesizeMatchSettingsFromLevel(UWorld* World)
{
	FSeinMatchSettings Out;
	if (!World) return Out;

	// Walk every SeinPlayerStart in the level. Build a Human slot per
	// PlayerStart with PlayerSlot > 0 (PlayerSlot 0 is "any slot" and not
	// part of the manifest). Faction/team flow through from the PlayerStart
	// actor. State is Human so PreRegisterMatchSlots pre-spawns each HQ at
	// world begin — both server and client run the same loop and produce
	// matching entity IDs (lockstep determinism).
	for (TActorIterator<ASeinPlayerStart> It(World); It; ++It)
	{
		const ASeinPlayerStart* Start = *It;
		if (!Start || Start->PlayerSlot <= 0) continue;

		FSeinMatchSlot Slot;
		Slot.SlotIndex   = Start->PlayerSlot;
		Slot.State       = ESeinSlotState::Human;
		Slot.FactionID   = Start->FactionID;
		Slot.TeamID      = Start->TeamID;
		Slot.DisplayName = FText::FromString(FString::Printf(TEXT("Slot %d"), Start->PlayerSlot));
		Out.Slots.Add(Slot);
	}

	// Sort by SlotIndex so iteration order is deterministic regardless of
	// editor placement / actor-iterator quirks. Without this, server and
	// client could iterate in different orders and produce mismatched
	// entity IDs even though the slot data is identical.
	Out.Slots.Sort([](const FSeinMatchSlot& A, const FSeinMatchSlot& B)
	{
		return A.SlotIndex < B.SlotIndex;
	});

	return Out;
}

#if WITH_EDITOR
void ASeinPlayerStart::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Editor-process snapshot — same pattern as ASeinActor. Conversion
	// runs once in the editor, the FFixedVector serializes to the .umap,
	// and every client (PC, ARM Mac, Surface ARM, mobile, console) reads
	// identical int64 bits at level load. Cross-arch lockstep safe.
	PlacedSimLocation = FFixedVector::FromVector(GetActorLocation());
	bSimLocationBaked = true;

	if (bFinished)
	{
		MarkPackageDirty();
	}
}
#endif
