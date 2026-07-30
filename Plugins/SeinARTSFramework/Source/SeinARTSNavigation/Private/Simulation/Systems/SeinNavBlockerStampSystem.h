/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavBlockerStampSystem.h
 * @brief   PreTick system that walks entities carrying FSeinExtentsComponent with
 *          bBlocksNav = true, expands each entity's Shapes into
 *          FSeinDynamicBlocker entries, and pushes the flat list to the
 *          active USeinNavigation. Pathfinding inside this same tick sees
 *          the fresh stamps.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Stamping/SeinStampShape.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Logging/LogMacros.h"

// LogSeinNavBlockerStamp (declared in SeinARTSNavigationLog.h): diagnostic log for
// the nav-blocker stamping pipeline. Off by default — `log LogSeinNavBlockerStamp
// Verbose` reports how many blockers each tick produces + which entities
// contributed. If the count is 0 no entity is supplying blocker data; if it's >0
// the issue is downstream (CollectDebugBlockerCells).
#include "SeinARTSNavigationLog.h"

/**
 * System: Nav Blocker Stamp
 * Phase: PreTick | Priority: 7
 *
 * Runs after SpatialHash (priority 5) and before pathfinding (which happens
 * in AbilityExecution phase via MoveToAction's TickAction). Walks the entity
 * pool in handle-index order (deterministic — same as the spatial hash
 * rebuild), filters to entities whose FSeinExtentsComponent has `bBlocksNav` set,
 * expands each entity's Shapes into FSeinDynamicBlocker entries, and pushes
 * the flat list to the nav.
 *
 * Each blocker carries the owning entity's `BlockedNavLayerMask`; the
 * pathfinding overlay-rebuild gates per-agent via mask intersection, so
 * water blockers (mask = Default) ignore amphibious agents (mask = N0).
 *
 * The complete handle/shape-ordered list is pushed every tick. Concrete
 * navigation implementations own exact change detection: the shipped A*
 * compares the ordered values and retains its overlay for an identical list.
 * This keeps the producer neutral and prevents pose bucketing or shape-count
 * shortcuts from hiding a real rasterized-cell change.
 */
class FSeinNavBlockerStampSystem final : public ISeinSystem
{
public:
	FSeinNavBlockerStampSystem(
		USeinNavigationSubsystem* InOwner,
		USeinNavigation* InNav)
		: Owner(InOwner)
		, Nav(InNav)
	{
	}

	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		USeinNavigation* NavPtr = Nav.Get();
		if (!NavPtr) return;
		USeinNavigationSubsystem* OwnerPtr = Owner.Get();
		if (!OwnerPtr
			|| !OwnerPtr->ValidateCommittedCanonicalStateBinding())
		{
			return;
		}

		Blockers.Reset();

		// Hoist component-storage lookups out of the per-entity loop.
		// `GetComponent<T>()` does a hashmap lookup by UScriptStruct* per
		// call; resolving the storage once turns the per-entity cost into
		// a single indexed access. Cheap on its own, compounding with the
		// dirty-bit work below.
		const ISeinComponentStorage* ExtentsStorage =
			World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		// Synthetic-radial fallback (no Extents authored) reads
		// FallbackFootprintRadius from the navigation component. The nav
		// component is the authoritative source for footprint info when
		// Extents is absent — matches the runtime cascade used by
		// USeinMovement::ResolveCollisionRadius (Extents → NavComp → 0).
		const ISeinComponentStorage* NavStorage =
			World.GetComponentStorageRaw(FSeinNavigationComponent::StaticStruct());

		World.GetEntityPool().ForEachEntity([&](
			FSeinEntityHandle Handle,
			const FSeinEntity& Entity)
		{
			const FFixedVector EntityPos = Entity.Transform.GetLocation();
			const FFixedQuaternion EntityRot = Entity.Transform.Rotation;

			const FSeinExtentsComponent* Extents = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(Handle))
				: nullptr;

			// Determine eligibility first; build blockers only after, so
			// non-eligible entities skip the list-append work too.
			bool bEligibleExtents = false;
			uint8 LayerMask = 0;

			if (Extents)
			{
				if (!Extents->bBlocksNav) return;
				if (Extents->Shapes.Num() == 0) return;
				if (Extents->BlockedNavLayerMask == 0) return;
				bEligibleExtents = true;
				LayerMask = Extents->BlockedNavLayerMask;
			}

			const FSeinNavigationComponent* NavData = nullptr;
			if (!bEligibleExtents)
			{
				NavData = NavStorage
					? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(Handle))
					: nullptr;
				if (!NavData || NavData->FallbackFootprintRadius <= FFixedPoint::Zero) return;
				LayerMask = 0x01;      // Default layer
			}

			// Build the new blocker list (existing logic). Always rebuilt
			// even when clean — cheap struct copies — so when dirty we
			// already have it ready to push without a second walk.
			if (bEligibleExtents)
			{
				for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
				{
					FSeinDynamicBlocker B;
					B.Shape = ExtShape.AsStampShape();
					B.EntityCenter = EntityPos;
					B.EntityRotation = EntityRot;
					B.Owner = Handle;
					B.BlockedNavLayerMask = Extents->BlockedNavLayerMask;
					Blockers.Add(B);
				}
			}
			else
			{
				FSeinStampShape Synthetic;
				Synthetic.Shape = ESeinStampShape::Radial;
				Synthetic.bEnabled = true;
				Synthetic.Radius = NavData->FallbackFootprintRadius;

				FSeinDynamicBlocker B;
				B.Shape = Synthetic;
				B.EntityCenter = EntityPos;
				B.EntityRotation = EntityRot;
				B.Owner = Handle;
				B.BlockedNavLayerMask = LayerMask;
				Blockers.Add(B);
			}
		});

		NavPtr->SetDynamicBlockers(Blockers);
		UE_LOG(LogSeinNavBlockerStamp, VeryVerbose,
			TEXT("Pushed %d exact nav blocker stamp(s)"), Blockers.Num());
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::WithCanonicalState(
			FName(TEXT("seinarts.navigation.dynamic_blocker_stamp")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::NavBlockerStamp,
			{FName(TEXT(
				"seinarts.navigation/async-path-continuation"))});
	}

private:
	TWeakObjectPtr<USeinNavigationSubsystem> Owner;
	TWeakObjectPtr<USeinNavigation> Nav;
	TArray<FSeinDynamicBlocker> Blockers;
};
