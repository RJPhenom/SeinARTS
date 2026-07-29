/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionBroadphaseSystem.h
 * @brief   PreTick system that rebuilds the collision broadphase
 *          (FSeinCollisionSpatialHash) from current collider positions.
 *          Replaces the old generic FSeinSpatialHashSystem — which gated on a
 *          navigation component and existed only to feed penetration. This one
 *          is purely collision-driven and has NO navigation dependency.
 *
 *          Each tick: rebuild the dynamic tier from every enabled Movable or
 *          Stationary collider; rebuild the static tier too, but only on ticks
 *          where the static set changed (Hash.IsStaticDirty()). A collider is any entity
 *          whose FSeinExtentsComponent has bCollisionEnabled, at least one
 *          Shape, and a non-None ObjectType.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // GetColliderBoundingRadius — shared with the resolver
#include "Components/SeinIdentityComponent.h"  // (dev diagnostic only) DisplayName for the collider-gate log
#include "Settings/PluginSettings.h"           // (dev diagnostic only) channel registry for the stale-channel check

/**
 * System: Collision Broadphase Rebuild
 * Phase: PreTick | Priority: 5
 *
 * Runs before the collision resolver (PostTick) so neighbour queries see this
 * tick's positions. Full clear+rebuild of the dynamic tier each tick; the
 * static tier is only rebuilt when dirty, so maps with lots of static geometry
 * (walls/buildings) pay for them once, not every tick.
 */
class FSeinCollisionBroadphaseSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Skip the (otherwise wasted) rebuild when nothing meaningful consumes the hash: the collision
		// resolver is OFF (null) AND the avoidance model is OFF (AvoidanceClass = None). The only other
		// reader, USeinMoveToAction's stall query, tolerates an empty hash (it just keeps pushing). The
		// avoidance INSTANCE lives in the Movement module, out of reach here, so its off-state is read
		// from AvoidanceClass — a per-client-identical setting, so the skip stays lockstep-deterministic.
		if (World.GetCollisionResolver() == nullptr)
		{
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			if (Settings && Settings->AvoidanceClass.IsNull())
			{
				return;
			}
		}

		FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

		const bool bRebuildStatic = Hash.IsStaticDirty();
		if (bRebuildStatic)
		{
			Hash.ClearStatic();
		}

		// Gather the dynamic colliders (Movable + Stationary) into a flat list, then
		// hand it to Hash.BuildDynamic in ONE call. The per-collider filtering +
		// GetColliderBoundingRadius below stays serial and cheap; BuildDynamic does the
		// expensive per-collider footprint cell-stamp in parallel and canonicalizes the
		// result, replacing the old per-collider Hash.InsertDynamic loop (and its
		// per-cell TMap hashing) with a sort-grid rebuild.
		TArray<FSeinCollisionSpatialHash::FDynamicColliderInput> DynamicColliders;
		DynamicColliders.Reserve(World.GetEntityPool().GetActiveCount());

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FSeinExtentsComponent* Extents = World.GetComponent<FSeinExtentsComponent>(Handle);

#if !UE_BUILD_SHIPPING
			// Mis-config warning: an entity with Extents shapes that won't actually
			// collide — collision off, no Object Type, OR an Object Type naming a
			// channel absent from the settings registry (every response then falls
			// through to Ignore). All three silently phase through. Warn once per
			// entity, against the SPAWNED runtime data — catches the stale-BP,
			// unset-ObjectType, and renamed/removed-channel traps. Stripped in shipping.
			if (Extents && Extents->Shapes.Num() > 0)
			{
				static TSet<int32> SeinLoggedColliderGate;
				if (!SeinLoggedColliderGate.Contains(Handle.Index))
				{
					SeinLoggedColliderGate.Add(Handle.Index);

					FString Why;
					if (!Extents->bCollisionEnabled)
					{
						Why = TEXT("Collision Enabled is off");
					}
					else if (Extents->ObjectType.Channel.IsNone())
					{
						Why = TEXT("Object Type is None");
					}
					else
					{
						bool bChannelRegistered = false;
						if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
						{
							for (const FSeinCollisionChannelDefinition& Ch : Settings->GetAllCollisionChannels())
							{
								if (Ch.Name == Extents->ObjectType.Channel) { bChannelRegistered = true; break; }
							}
						}
						if (!bChannelRegistered)
						{
							Why = FString::Printf(TEXT("Object Type '%s' is not a registered collision channel"),
								*Extents->ObjectType.Channel.ToString());
						}
					}

					if (!Why.IsEmpty())
					{
						FString Name(TEXT("(no identity)"));
						if (const FSeinIdentityComponent* Ident = World.GetComponent<FSeinIdentityComponent>(Handle))
						{
							Name = Ident->DisplayName.IsEmptyOrWhitespace()
								? Ident->IdentityTag.ToString() : Ident->DisplayName.ToString();
						}
						UE_LOG(LogTemp, Warning,
							TEXT("[SeinCollision] '%s' (entity %d) has Extents shapes but won't collide — %s. Fix on the entity's Extents > Collision section (channels live in Project Settings > Plugins > SeinARTS > Collision)."),
							*Name, Handle.Index, *Why);
					}
				}
			}
#endif

			if (!Extents || !Extents->bCollisionEnabled) return;
			if (Extents->Shapes.Num() == 0 || Extents->ObjectType.Channel.IsNone()) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();
			const FFixedPoint Radius = SeinExtentsHelpers::GetColliderBoundingRadius(*Extents);
			if (Extents->Mobility == ESeinCollisionMobility::Static)
			{
				// Static positions don't change; only (re)insert on a dirty pass.
				if (bRebuildStatic)
				{
					Hash.InsertStatic(Handle, Pos, Radius);
				}
			}
			else
			{
				// Movable AND Stationary live in the per-tick dynamic tier — both
				// can change position (Stationary is unpushable, but script/ability-
				// moved), so neither can be cached in the static tier like Static.
				// Collected here; stamped in one parallel BuildDynamic pass below.
				DynamicColliders.Add(FSeinCollisionSpatialHash::FDynamicColliderInput{ Handle, Pos, Radius });
			}
		});

		// One batched rebuild of the dynamic sort grid (parallel per-collider stamp +
		// canonicalizing sort). Replaces the per-collider InsertDynamic + ClearDynamic.
		Hash.BuildDynamic(DynamicColliders);

		if (bRebuildStatic)
		{
			Hash.FinishStaticRebuild();
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.collision_broadphase")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::CollisionBroadphase);
	}
};
