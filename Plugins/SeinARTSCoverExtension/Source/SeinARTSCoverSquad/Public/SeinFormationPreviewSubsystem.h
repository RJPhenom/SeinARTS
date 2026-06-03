/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewSubsystem.h
 * @brief   Per-local-player coordinator for the destination preview decals.
 *
 *          Listens to ASeinPlayerController's OnSelectionChanged + OnCursorUpdated
 *          delegates. Each tick (driven by OnCursorUpdated) it:
 *            1. Bails if the targeter is active (its preview takes precedence)
 *            2. Resolves the local selection to a member-handle list
 *            3. Calls USeinCommandBrokerBPFL::SeinComputeFormationPreview to get
 *               per-member projected positions
 *            4. Pushes those positions into the spawned ASeinFormationPreviewActor
 *
 *          Phase 1: neutral decals only — no cover queries, no color-coding.
 *          Phase 2 will color decals by per-position cover quality (heavy /
 *          light / negative / open) via a USeinCoverQuery interface. Phase 3
 *          will plumb cover slots into the formation solver itself for the
 *          "snap to cover" behavior.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "SeinFormationPreviewSubsystem.generated.h"

class ASeinFormationPreviewActor;
class ASeinPlayerController;

/**
 * Implements FTickableGameObject in addition to ULocalPlayerSubsystem because
 * the PC isn't available at LP-subsystem-Initialize time and isn't guaranteed
 * to exist when `PostLoadMapWithWorld` fires either — PIE bootstrap orders
 * Initialize → world load → PC spawn → BeginPlay, and the load-map delegate
 * lands before PC. A single retry-on-map-load wasn't enough in practice (the
 * binding silently failed and was never re-attempted). Tick-based lazy bind
 * polls every frame until BoundPC is set, then early-outs cheaply forever.
 */
UCLASS()
class SEINARTSCOVERSQUAD_API USeinFormationPreviewSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	// Public API
	// ====================================================================================================

	/** Force a refresh of the preview from the current cursor position. Useful
	 *  for game code that wants to immediately update decals after a non-cursor
	 *  state change (e.g., a control-group recall that changes selection
	 *  mid-frame between cursor ticks). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Cover|Preview")
	void RefreshPreview();

	/** True iff the subsystem currently has an active preview actor and the
	 *  cursor + selection state would cause decals to be visible. UI / debug
	 *  surfaces can read this for "yes, the player is currently seeing the
	 *  preview" displays. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover|Preview")
	bool IsPreviewVisible() const { return bIsVisible; }

private:
	// PC delegate handlers
	UFUNCTION()
	void HandleSelectionChanged();

	UFUNCTION()
	void HandleCursorUpdated(FVector CursorWorld, bool bValidTrace);

	// Internals
	// ====================================================================================================

	/** Lazily spawn the preview actor on first use. Reads the configured class
	 *  from `USeinARTSCoverSettings::FormationPreviewActorClass` (falls back to
	 *  `ASeinFormationPreviewActor::StaticClass()` if unset). */
	void EnsurePreviewActorSpawned();

	/** Subscribe to the local PC's delegates. Called on Initialize and
	 *  re-called whenever a new PC becomes available (post-travel). */
	void HookPlayerControllerDelegates();

	/** Drop subscriptions on the prior PC (if any) before re-binding to a
	 *  fresh one. Idempotent if no PC was previously bound. */
	void UnhookPlayerControllerDelegates();

	/** Resolve the local PC's selection to a member-handle list suitable for
	 *  SeinComputeFormationPreview. For squad-actor selections, expands to the
	 *  squad's live member entities. For non-squad actors, returns the actor's
	 *  own entity handle. */
	TArray<struct FSeinEntityHandle> ResolveSelectionToMembers(ASeinPlayerController* PC) const;

	/** Tear down the preview — hides actor, clears visibility flag. */
	void HidePreview();

	/** Convert FFixedVector array (sim-side) to FVector array (render-side). */
	static TArray<FVector> ConvertPositions(const TArray<struct FFixedVector>& In);

private:
	UPROPERTY(Transient)
	TObjectPtr<ASeinFormationPreviewActor> PreviewActor = nullptr;

	/** Last PC we bound to. Cleared on travel + rebound when a new local PC
	 *  becomes available. Weak so a PC destroy doesn't keep the subsystem
	 *  holding stale memory. */
	TWeakObjectPtr<ASeinPlayerController> BoundPC;

	/** Most recent traced cursor position. Updated on every OnCursorUpdated.
	 *  Used by RefreshPreview() to recompute without waiting for the next tick. */
	FVector LastCursorWorld = FVector::ZeroVector;

	/** True when the last cursor delegate fire reported a valid trace —
	 *  RefreshPreview consults this so a forced refresh during an off-world
	 *  trace doesn't try to render decals at (0,0,0). */
	bool bLastCursorValid = false;

	/** True when decals are currently shown. Mirrored to the actor visibility
	 *  state for IsPreviewVisible() reads. */
	bool bIsVisible = false;

	// ----------------------------------------------------------------
	// Cover-quality query throttle
	// ----------------------------------------------------------------
	// The per-cell tint query (`Cover->QueryBestCoverQualityAt` for each
	// formation position) is O(Members × Providers) per refresh — it's
	// the dominant cost when many cover providers are on the map. We
	// throttle it: only re-query when the cursor has moved more than
	// `CoverTintCursorThresholdSq` cm² OR the selection changed
	// (`bCoverTintDirty`). Between refreshes we reuse the cached
	// `CachedCoverQualities` array. A 25cm threshold is below the
	// per-cell decal size so the tint update lag is visually
	// imperceptible.

	/** Cached cover-quality tag per formation position from the last
	 *  query. Reused on subsequent refreshes that fall within the
	 *  cursor-delta gate. */
	UPROPERTY(Transient)
	TArray<FGameplayTag> CachedCoverQualities;

	/** Cursor position at the time `CachedCoverQualities` was computed.
	 *  Subsequent refreshes compare against this to decide whether to
	 *  re-query or reuse. */
	FVector LastTintQueryCursor = FVector::ZeroVector;

	/** True when the cached qualities should be invalidated on the next
	 *  refresh regardless of cursor delta (e.g. selection changed,
	 *  cover provider added/removed in PIE). Set by HandleSelectionChanged
	 *  and the post-load-map hook. */
	bool bCoverTintDirty = true;
};
