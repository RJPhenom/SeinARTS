/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerBPFL.h
 * @brief   BP surface for CommandBroker introspection + dispatch (DESIGN §5).
 *          Reads are BlueprintPure; writes (IssueOrder) go through the command
 *          buffer so the txn log keeps one entry per player click regardless
 *          of selection size.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/Vector.h"
#include "Components/SeinCommandBrokerData.h"
#include "Brokers/SeinBrokerTypes.h"
#include "SeinCommandBrokerBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Command Broker Library"))
class SEINARTSCOREENTITY_API USeinCommandBrokerBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	/** Read FSeinCommandBrokerData for a broker entity. Returns false and logs if the
	 *  handle is invalid or the entity is not a broker; OutData untouched on failure. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Broker Data"))
	static bool SeinGetBrokerData(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle, FSeinCommandBrokerData& OutData);

	/** Returns the broker currently holding this member, or an invalid handle if none. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Current Broker"))
	static FSeinEntityHandle SeinGetCurrentBroker(const UObject* WorldContextObject, FSeinEntityHandle Member);

	/** Returns the live member handles on this broker. Empty on invalid handle. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Broker Members"))
	static TArray<FSeinEntityHandle> SeinGetBrokerMembers(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle);

	/** Returns the broker centroid (zero if invalid). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Broker Centroid"))
	static FFixedVector SeinGetBrokerCentroid(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle);

	/** Returns the click context of the currently-dispatched order. Empty container
	 *  if the broker is idle or invalid. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Broker Active Order Context"))
	static FGameplayTagContainer SeinGetBrokerActiveOrderContext(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle);

	/** Number of orders queued (including the active one). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Broker Queue Length"))
	static int32 SeinGetBrokerQueueLength(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle);

	// Dispatch
	// ====================================================================================================

	/** Enqueue a broker order. Creates or reuses a broker for the given member set
	 *  (DESIGN §5 "one broker per member" invariant — existing members are evicted
	 *  from their prior brokers). All txn logging goes through the command buffer:
	 *  this helper builds the command, invokes `World->EnqueueCommand`, and returns.
	 *  If `bQueueCommand` is true and the members already share a broker, the order
	 *  is appended to that broker's queue without spawning a new one — the new
	 *  order is subset-targeted at `Members` if `Members` is a strict subset of
	 *  the shared broker's live member list.
	 *
	 *  @param CommandContext — raw click context (RightClick + Target.* + designer
	 *                          tags). Resolver interprets per-member to pick which
	 *                          ability to activate. For direct "everyone does X"
	 *                          dispatch, include the ability tag in the container.
	 *  @param TargetEntity   — optional target entity for the order.
	 *  @param TargetLocation — target world location (move destination, etc.).
	 *  @param bQueueCommand  — true = shift-chained (append), false = replace queue.
	 *  @param FormationEnd   — optional second endpoint for drag orders. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Broker",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Issue Broker Order"))
	static void SeinIssueBrokerOrder(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID,
		const TArray<FSeinEntityHandle>& Members,
		const FGameplayTagContainer& CommandContext,
		FSeinEntityHandle TargetEntity,
		FFixedVector TargetLocation,
		bool bQueueCommand = false,
		FFixedVector FormationEnd = FFixedVector());

	// Preview
	// ====================================================================================================

	/**
	 * Compute the would-be formation positions for a hypothetical move/attack target.
	 *
	 * Pure-compute preview through the same resolver and selection-plan providers used by
	 * command construction, without spawning a broker or mutating sim state. The shipped
	 * native preview subsystem/player controller carries the exact displayed artifact into
	 * its command. The public Blueprint `Issue Broker Order` node cannot accept this node's
	 * guide points, formation tag, or frozen artifact and recomputes with defaults; custom
	 * Blueprint input paths must not assume preview/commit parity until that API is resolved.
	 *
	 * Resolver dispatch:
	 *   - Single-squad selection (every member shares the same FSeinSquadMemberComponent::SquadEntity):
	 *     uses the squad's pooled resolver instance; reads centroid + facing from the squad's
	 *     FSeinCommandBrokerData and the per-squad re-match flags (bReassignSlotsLateral /
	 *     bReassignSlotsDepth) from FSeinSquadComponent. Layout reflects authored slot offsets exactly.
	 *   - Mixed / multi-entity selection: uses the framework's default resolver class
	 *     (settings.DefaultBrokerResolverClass, or USeinDefaultCommandBrokerResolver if
	 *     unset) via its CDO. Centroid = average of member transforms; facing = identity; the
	 *     formation-level re-match flags (default both on, i.e. 2-D) come off that resolver.
	 *
	 * No side effects. Cost is provider-dependent; Cover may run its exact selection-wide
	 * allocator, so presentation callers should use the shipped preview cadence/cache path.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker|Formation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Compute Formation Preview"))
	static FSeinFormationLayout SeinComputeFormationPreview(
		const UObject* WorldContextObject,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector TargetLocation,
		const TArray<FFixedVector>& GuidePoints,
		FGameplayTag FormationTag);

	/** Native preview/commit seam. Returns the same layout as the Blueprint
	 *  preview API and also exposes the exact per-member artifact after all
	 *  stable-ID selection providers have run. */
	static FSeinFormationLayout ComputeFormationDestinationArtifact(
		const UObject* WorldContextObject,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector TargetLocation,
		const TArray<FFixedVector>& GuidePoints,
		FGameplayTag FormationTag,
		FSeinPlayerID OrderingPlayer,
		bool bQueueCommand,
		TArray<FSeinFrozenDestination>& OutDestinationArtifact,
		bool* OutSucceeded = nullptr);

	/** Per-broker formation anchors (internal C++ helper, NOT BP-exposed). Lays the persistent-broker
	 *  (squad) entities out as ELEMENTS of the gesture `FormationTag` formation — each squad is one
	 *  element, sized by its FSeinCommandBrokerData::FormationRadius (its whole footprint) via the same
	 *  footprint-aware `ResolveFormationLayout` loose units use. So a multi-squad order takes the chosen
	 *  shape (Ring/Wedge/Grid/Box/…) instead of the old hardcoded box/row; each squad then lays its own
	 *  members around the returned anchor (USeinSlotFormation). An invalid `FormationTag` falls to the
	 *  resolver's default formation. N == 0 → empty; N == 1 → the single element centres on ClickTarget.
	 *  Index-aligned with `Brokers`. Shared by the commit (USeinWorldSubsystem::ProcessCommands) and
	 *  SeinComputeFormationPreview so the two can never drift. `OutFacings` returns each broker's
	 *  position-dependent facing (radial in a ring, drag-perp in a box, …), index-aligned with `Brokers`. */
	static TArray<FFixedVector> ComputeMultiBrokerAnchors(
		USeinWorldSubsystem& World,
		const TArray<FSeinEntityHandle>& Brokers,
		FFixedVector ClickTarget,
		const TArray<FFixedVector>& GuidePoints,
		FGameplayTag FormationTag,
		TArray<FFixedQuaternion>& OutFacings);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
