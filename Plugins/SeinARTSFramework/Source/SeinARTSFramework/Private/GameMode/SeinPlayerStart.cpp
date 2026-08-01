/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerStart.cpp
 * @brief   RTS player start implementation.
 */

#include "GameMode/SeinPlayerStart.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Settings/PluginSettings.h"
#include "UObject/ObjectSaveContext.h"

ASeinPlayerStart::ASeinPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FSeinMatchSettings ASeinPlayerStart::SynthesizeMatchSettingsFromLevel(UWorld* World)
{
	FSeinMatchSettings Out;
	if (!World) return Out;
	if (const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>())
	{
		Out.Extensions = Settings->DefaultMatchExtensions;
	}

	// Walk every SeinPlayerStart in the level. Build a Human slot per
	// PlayerStart with PlayerSlot > 0 (PlayerSlot 0 is "any slot" and not
	// part of the manifest). Faction/team flow through from the PlayerStart
	// actor. State is Human so the shared bootstrap transaction registers each
	// player and optional start entity in canonical slot order on every peer.
	for (TActorIterator<ASeinPlayerStart> It(World); It; ++It)
	{
		const ASeinPlayerStart* Start = *It;
		if (!Start || Start->PlayerSlot <= 0) continue;

		FSeinMatchSlot Slot;
		Slot.SlotIndex   = Start->PlayerSlot;
		Slot.State       = ESeinSlotState::Human;
		Slot.FactionID   = Start->FactionID;
		Slot.TeamID      = Start->TeamID;
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

void ASeinPlayerStart::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// Self-heal a legacy placement the moment it loads in the editor:
	// levels saved before PlacedSimTransform existed deserialize with
	// bSimTransformBaked=false and would fail the bootstrap's fail-closed
	// baked-transform check forever (PostEditMove only fires on an actual
	// move, and a clean package never reaches PreSave). Baking here makes
	// the in-memory actor — and every PIE duplicate of it — valid
	// immediately; the upgrade persists whenever the level is next saved.
	// Editor-only on purpose: cooked clients must NOT convert at load
	// (float→fixed is not bit-identical across architectures), so an
	// unbaked level in a shipped build still fails closed.
	// Components are not registered yet at PostLoad, so the composed
	// GetActorTransform() is not trustworthy here — read the serialized
	// relative transform off the root, which IS world space for a placed
	// (unattached) actor. If somehow rootless, leave unbaked: the check
	// stays fail-closed rather than baking an identity transform.
	if (!bSimTransformBaked && !IsTemplate())
	{
		if (const USceneComponent* Root = GetRootComponent())
		{
			PlacedSimTransform = FFixedTransform::FromTransform(FTransform(
				Root->GetRelativeRotation(),
				Root->GetRelativeLocation(),
				Root->GetRelativeScale3D()));
			bSimTransformBaked = true;
		}
	}
#endif
}

#if WITH_EDITOR
void ASeinPlayerStart::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Editor-process snapshot — same pattern as ASeinActor. Conversion
	// runs once in the editor, the FFixedTransform serializes to the .umap,
	// and every client (PC, ARM Mac, Surface ARM, mobile, console) reads
	// identical int64 bits at level load. Cross-arch lockstep safe.
	PlacedSimTransform = FFixedTransform::FromTransform(GetActorTransform());
	bSimTransformBaked = true;

	if (bFinished)
	{
		MarkPackageDirty();
	}
}

void ASeinPlayerStart::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);

	// Upgrade-on-save for placements that predate the baked snapshot: the
	// bootstrap fails closed on an unbaked start with "re-save the level",
	// so a plain save must actually perform the bake. Already-baked starts
	// are left untouched — only PostEditMove re-bakes, when the designer
	// actually moved the actor.
	if (!bSimTransformBaked && !IsTemplate())
	{
		PlacedSimTransform =
			FFixedTransform::FromTransform(GetActorTransform());
		bSimTransformBaked = true;
	}
}
#endif
