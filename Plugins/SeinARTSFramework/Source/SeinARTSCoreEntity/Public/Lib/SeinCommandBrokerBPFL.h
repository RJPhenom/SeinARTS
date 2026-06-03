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
	 * Pure-compute counterpart to `SeinIssueBrokerOrder` — returns the same per-member
	 * positions the broker resolver would dispatch members to, without spawning a broker
	 * or mutating any state. Primary consumer is the cover module's destination preview
	 * decals (hover preview shows where each squad member will end up before the player
	 * commits a right-click). Future consumers: targeter spec previews that want to
	 * visualize formation positions while the cursor sweeps over candidate targets.
	 *
	 * Resolver dispatch:
	 *   - Single-squad selection (every member shares the same FSeinSquadMemberComponent::SquadEntity):
	 *     uses the squad's pooled resolver instance; reads centroid + facing from the
	 *     squad's FSeinCommandBrokerData and bInvertSlotOrderWhenMovingBackward from
	 *     FSeinSquadComponent. Layout reflects authored slot offsets exactly.
	 *   - Mixed / multi-entity selection: uses the framework's default resolver class
	 *     (settings.DefaultBrokerResolverClass, or USeinDefaultCommandBrokerResolver if
	 *     unset) via its CDO. Centroid = average of member transforms; facing = identity;
	 *     bInvertWhenBackward = false. Layout is the symmetric grid.
	 *
	 * No side effects. Cheap enough to call every cursor tick (one virtual dispatch +
	 * one pass over Members for centroid + one pass for grid/slot positions).
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Broker|Formation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Compute Formation Preview"))
	static FSeinFormationLayout SeinComputeFormationPreview(
		const UObject* WorldContextObject,
		const TArray<FSeinEntityHandle>& Members,
		FFixedVector TargetLocation);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
