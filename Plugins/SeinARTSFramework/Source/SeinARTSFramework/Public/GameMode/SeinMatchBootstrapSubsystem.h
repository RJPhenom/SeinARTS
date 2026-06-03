/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapSubsystem.h
 * @brief   Client-side mirror of ASeinGameMode::PreRegisterMatchSlots.
 *
 * The server's GameMode runs in InitGame and pre-registers each match slot
 * (RegisterPlayer + spawn the slot's start entity) before any controller
 * connects. This produces deterministic entity IDs 1..N for the slots'
 * starting units.
 *
 * Clients have NO GameMode (UE only instantiates GameMode on the server),
 * so without this subsystem each client's sim starts empty of slot
 * starts. When the server's sim spawns Player(2)'s infantry as entity
 * Handle(2:1), the client's sim has nothing at that ID — and any move
 * command targeting that handle silently fails. Lockstep is dead from
 * frame zero.
 *
 * This subsystem mirrors the server-side pre-registration loop on every
 * client at OnWorldBeginPlay. Match settings live on ASeinWorldSettings
 * (replicated implicitly because it's a level actor, identical on every
 * machine), so the inputs to the loop are bit-identical, and the
 * resulting entity IDs match. Server still does its own pre-reg via
 * GameMode — this subsystem only runs when NetMode == NM_Client.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinMatchBootstrapSubsystem.generated.h"

class USeinWorldSubsystem;
struct FSeinMatchSettings;
struct FSeinWorldSnapshot;

UCLASS()
class SEINARTSFRAMEWORK_API USeinMatchBootstrapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** Per-slot pre-registration loop (mirror of SeinGameMode's). Slot order
	 *  follows match settings array order (deterministic), and SeinPlayerStart
	 *  lookup is by SlotIndex match (not iteration order), so resulting
	 *  entity IDs are identical across server (where GameMode runs this in
	 *  InitGame) and every client (where this subsystem runs it on
	 *  OnWorldBeginPlay). */
	void RunSlotPreRegistration(UWorld& InWorld, USeinWorldSubsystem& Sub, const FSeinMatchSettings& Settings);

	/** Snapshot capture hook (Framework-side): stamp the local camera pose
	 *  onto the snapshot. Bound in OnWorldBeginPlay to USeinWorldSubsystem's
	 *  OnCaptureSnapshotPostSim delegate. SeinARTSCoreEntity can't include
	 *  ASeinCameraPawn (cycle), so the inversion happens here. */
	void HandleSnapshotCapture(FSeinWorldSnapshot& Snapshot);

	/** Mirror — restore local camera pose from snapshot. */
	void HandleSnapshotRestore(const FSeinWorldSnapshot& Snapshot);

	FDelegateHandle SnapshotCaptureHandle;
	FDelegateHandle SnapshotRestoreHandle;
};
