/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewSubsystem.h
 * @brief   Per-local-player coordinator for the destination preview.
 *
 *          BASE feature (ported from the Cover extension). Listens to
 *          ASeinPlayerController's OnSelectionChanged + OnCursorUpdated. Each tick
 *          (driven by the cursor delegate) it:
 *            1. Bails if the targeter is active (its preview takes precedence)
 *            2. Resolves the local selection to a member-handle list
 *            3. Calls USeinCommandBrokerBPFL::SeinComputeFormationPreview for the
 *               per-member projected positions (the SAME dry-run the commit uses)
 *            4. Optionally fetches per-cell quality tags from
 *               USeinWorldSubsystem::PreviewQualityProvider (an extension hook —
 *               e.g. the Cover extension supplies cover quality; unbound = neutral)
 *            5. Pushes positions (+ qualities) into the spawned preview actor
 *
 *          The framework owns this; Cover/Squad are consumers. Quality tinting is
 *          entirely optional and supplied by whoever binds the provider delegate.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "SeinFormationPreviewSubsystem.generated.h"

class ASeinFormationPreviewActor;
class ASeinPlayerController;
struct FSeinEntityHandle;
struct FFixedVector;

/**
 * FTickableGameObject in addition to ULocalPlayerSubsystem because the PC isn't
 * available at LP-subsystem-Initialize time and the load-map delegate can land
 * before the PC spawns. Tick-based lazy bind polls until BoundPC is set, then
 * early-outs cheaply forever.
 */
UCLASS()
class SEINARTSFRAMEWORK_API USeinFormationPreviewSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
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

	/**
	 * Stop ticking, detach from engine/player delegates, and destroy the
	 * transient preview before module withdrawal.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** Force a refresh of the preview from the current cursor / drag state. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Preview")
	void RefreshPreview();

	/** True iff a preview actor exists and the current state would show decals. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Preview")
	bool IsPreviewVisible() const { return bIsVisible; }

	/** Return the exact artifact currently rendered when its complete command key
	 *  matches. False prevents stale preview state from entering a command. */
	bool TryGetDisplayedDestinationArtifact(
		const TArray<FSeinEntityHandle>& Members,
		const FFixedVector& TargetLocation,
		const TArray<FFixedVector>& GuidePoints,
		FGameplayTag FormationTag,
		bool bQueueCommand,
		TArray<FSeinFrozenDestination>& OutArtifact) const;

private:
	// PC delegate handlers
	UFUNCTION()
	void HandleSelectionChanged();

	UFUNCTION()
	void HandleCursorUpdated(FVector CursorWorld, bool bValidTrace);

	/** Lazily spawn the preview actor on first use. Class from
	 *  USeinARTSCoreSettings::FormationPreviewActorClass (falls back to
	 *  ASeinFormationPreviewActor::StaticClass()). */
	void EnsurePreviewActorSpawned();

	/** Subscribe to the local PC's delegates (re-callable on travel). */
	void HookPlayerControllerDelegates();

	/** Drop subscriptions on the prior PC. Idempotent. */
	void UnhookPlayerControllerDelegates();

	/** Resolve the local PC's selection to a member-handle list (squads expand to
	 *  live members; lone units return their own handle). All-or-nothing preview
	 *  opt-in: any opted-out member suppresses the whole preview. */
	TArray<FSeinEntityHandle> ResolveSelectionToMembers(ASeinPlayerController* PC) const;

	/** Tear down the preview — hides actor, clears visibility. */
	void HidePreview();

	/** Convert FFixedVector array (sim-side) to FVector array (render-side). */
	static TArray<FVector> ConvertPositions(const TArray<FFixedVector>& In);

private:
	UPROPERTY(Transient)
	TObjectPtr<ASeinFormationPreviewActor> PreviewActor = nullptr;

	/** Last PC we bound to. Weak so a PC destroy doesn't keep stale memory. */
	TWeakObjectPtr<ASeinPlayerController> BoundPC;

	/** Most recent traced cursor position (from OnCursorUpdated). */
	FVector LastCursorWorld = FVector::ZeroVector;

	/** True when the last cursor delegate fire reported a valid trace. */
	bool bLastCursorValid = false;

	/** True when decals are currently shown. */
	bool bIsVisible = false;

	// Quality-tag query throttle.
	// The optional per-cell quality query (USeinWorldSubsystem::PreviewQualityProvider)
	// can be O(Members × providers); re-query only when the cursor moved past a
	// threshold, the selection changed, or the cell count changed — otherwise reuse
	// the cached tags. Threshold is below decal size so the lag is imperceptible.

	/** Cached per-position quality tags from the last provider query. */
	UPROPERTY(Transient)
	TArray<FGameplayTag> CachedQualities;

	/** Cursor position when CachedQualities was computed. */
	FVector LastQualityQueryCursor = FVector::ZeroVector;

	/** Force a quality re-query next refresh regardless of cursor delta. */
	bool bQualityDirty = true;

	/** Last exact presentation key that produced the visible layout. Repeated
	 *  cursor broadcasts between fixed simulation ticks are common at high
	 *  render rates; they must not rerun the O(N^2) slot match when neither
	 *  simulation state nor gesture input changed. */
	FVector LastLayoutCursor = FVector::ZeroVector;
	int32 LastLayoutSimTick = MIN_int32;
	int32 LastLayoutDragPointCount = INDEX_NONE;
	bool bLastLayoutWasCommandDrag = false;
	bool bLayoutDirty = true;
	/** Pose fingerprint of the displayed members at the last real solve —
	 *  render-only bookkeeping for the unchanged-input re-solve skip. */
	uint32 LastLayoutMemberPoseHash = 0;

	TArray<FSeinFrozenDestination> DisplayedDestinationArtifact;
	TArray<FSeinEntityHandle> DisplayedArtifactMembers;
	FFixedVector DisplayedArtifactTarget;
	TArray<FFixedVector> DisplayedArtifactGuidePoints;
	FGameplayTag DisplayedArtifactFormationTag;
	bool bDisplayedArtifactQueueCommand = false;

	/** Prevents FTickableGameObject from invoking withdrawn module code. */
	bool bModuleUnloadStateReleased = false;
};
