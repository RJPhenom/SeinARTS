/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinPlayerStart.cpp
 * @author       RJ Macklem
 * @created      2 Jun 2026
 * @latest       4 Sep 2026
 * @brief        Keeps authored player-start transforms synchronized for deterministic spawning.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "GameMode/SeinPlayerStart.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Misc/CoreMisc.h"
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

#if WITH_EDITOR
void ASeinPlayerStart::RefreshPlacedSimTransform()
{
	UWorld* World = GetWorld();
	if (IsTemplate() || !World || World->WorldType != EWorldType::Editor
		|| IsRunningCookCommandlet() || !GetRootComponent()
		|| !GetRootComponent()->IsRegistered())
	{
		return;
	}

	const FFixedTransform Current = FFixedTransform::FromTransform(GetActorTransform());
	if (!bSimTransformBaked || PlacedSimTransform != Current)
	{
		Modify();
		PlacedSimTransform = Current;
		bSimTransformBaked = true;
		MarkPackageDirty();
	}
}

void ASeinPlayerStart::UnbindAuthoringTransform()
{
	if (USceneComponent* Root = AuthoringTransformRoot.Get())
	{
		Root->TransformUpdated.Remove(AuthoringTransformHandle);
	}
	AuthoringTransformRoot.Reset();
	AuthoringTransformHandle.Reset();
}

void ASeinPlayerStart::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UnbindAuthoringTransform();
	UWorld* World = GetWorld();
	if (IsTemplate() || !World || World->WorldType != EWorldType::Editor
		|| IsRunningCookCommandlet())
	{
		return;
	}

	if (USceneComponent* Root = GetRootComponent())
	{
		AuthoringTransformRoot = Root;
		AuthoringTransformHandle = Root->TransformUpdated.AddWeakLambda(this,
			[this](USceneComponent* UpdatedComponent, EUpdateTransformFlags, ETeleportType)
			{
				if (UpdatedComponent == GetRootComponent())
				{
					RefreshPlacedSimTransform();
				}
			});
	}
	// Registration provides the composed world transform, including attachments.
	// This also repairs legacy or stale snapshots when their map opens in editor.
	RefreshPlacedSimTransform();
}

void ASeinPlayerStart::PostUnregisterAllComponents()
{
	UnbindAuthoringTransform();
	Super::PostUnregisterAllComponents();
}

void ASeinPlayerStart::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	RefreshPlacedSimTransform();
}

void ASeinPlayerStart::PostEditUndo()
{
	Super::PostEditUndo();
	RefreshPlacedSimTransform();
}

void ASeinPlayerStart::PreSave(FObjectPreSaveContext SaveContext)
{
	// Saving must repair an already-baked stale snapshot too. Never convert in
	// cook or game worlds: peers must retain the author's serialized int64 bits.
	if (!SaveContext.IsCooking())
	{
		RefreshPlacedSimTransform();
	}
	Super::PreSave(SaveContext);
}

void ASeinPlayerStart::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
{
	// ANavigationObjectBase's no-argument override hides the actor overload.
	AActor::PostEditUndo(TransactionAnnotation);
	RefreshPlacedSimTransform();
}
#endif
