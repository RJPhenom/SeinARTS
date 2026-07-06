/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSystem.h
 * @brief   Per-tick lifecycle + lazy initialization for squad entities.
 *          PostTick phase, runs BEFORE the broker system so dead-member strips
 *          reach the broker on the same tick.
 *
 *          Per squad, per tick:
 *            0. (Lazy init, first time only) If the squad entity has an
 *               FSeinSquadComponent but no FSeinCommandBrokerData yet, run
 *               the initial cascade: spawn each slot's Entity, wire the
 *               member back-refs (FSeinSquadMemberComponent +
 *               FSeinBrokerMembershipData), build the persistent broker
 *               with the per-squad DispatchResolverClass, promote the first
 *               occupant as leader. Detected by absence of broker — no
 *               separate "initialized" flag needed.
 *            1. Strip slot occupants whose entity is no longer alive; emit
 *               SquadMemberDied for each strip.
 *            2. Promote a new leader if the current leader handle is invalid.
 *            3. Update the squad entity's own transform to the live member
 *               centroid (so render-side widgets / banners follow the group).
 *            4. Sync broker.Centroid + anchor from the recomputed centroid.
 *            5. Decrement per-slot CurrentCooldown toward zero.
 *            6. Tick the reinforce queue: progress front entry, on completion
 *               spawn the slot's entity class at the squad's transform and wire
 *               the new member through the standard fill path.
 *            7. Cull the squad when all slots are empty AND the reinforce
 *               queue is empty AND the broker has no pending orders.
 *
 *          One-subsystem-per-feature: init, lifecycle, and reinforce all
 *          live here (mirrors USeinCoverSubsystem's "one subsystem owns
 *          all the work" pattern). Squad-related code outside this file is
 *          limited to data structs, the dispatch resolver, BPFLs, and the
 *          reinforce ability — none of them hold lifecycle responsibility.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Formations/SeinFormation.h"
#include "SeinSquadDispatchResolver.h"
#include "SeinARTSSquadSettings.h"
#include "Events/SeinVisualEvent.h"
#include "Types/Entity.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinSquadSystem, Log, All);
inline DEFINE_LOG_CATEGORY(LogSeinSquadSystem);

/**
 * System: Squad
 * Phase: PostTick | Priority: 30
 *
 * Runs BEFORE FSeinCommandBrokerSystem (priority 40) so dead-member strips
 * propagate to the broker's Members array on the same tick — the broker's
 * own dead-strip pass becomes a no-op for entities the squad already
 * cleaned up, and the broker's executing-order completion check sees the
 * up-to-date member list.
 */
class FSeinSquadSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		TArray<FSeinEntityHandle> CullList;

		// Per-tick diagnostic — only log once every ~30 ticks (1 sec at 30Hz)
		// so steady-state spam stays bounded but we can confirm the system
		// is alive + reaching the per-entity loop.
		static int32 TickCounter = 0;
		const bool bDiagThisTick = (++TickCounter % 30) == 1;
		int32 EntitiesSeen = 0;
		int32 SquadsFound = 0;

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			++EntitiesSeen;

			FSeinSquadComponent* Squad = World.GetComponent<FSeinSquadComponent>(Handle);
			if (!Squad) return;

			++SquadsFound;

			FSeinCommandBrokerData* Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);

			// 0. Lazy initialization: detected by absence of the broker. Runs
			// once on the first tick after the squad entity spawns. Spawns each
			// slot's Entity at SquadXform * Slot.OffsetTransform, wires member
			// back-refs, attaches the persistent broker with bSelfCullOnEmpty=
			// false, registers the squad's dispatch resolver, promotes the
			// first live occupant as leader. After this runs the broker exists
			// and the cascade won't fire again for this squad.
			//
			// Empty-slot-list squads (all slots have no Entity class, or Slots
			// is itself empty) get the broker but no members → bAllSlotsEmpty
			// at step 7 culls them on the same tick. That's the right call —
			// a squad recipe with no spawn potential isn't useful. Designers
			// who want a "lazy" squad placeholder should pre-queue a reinforce
			// entry so the cull condition doesn't fire.
			if (!Broker)
			{
				const FSeinPlayerID OwnerPlayer = World.GetEntityOwner(Handle);
				const FFixedTransform SquadXform = Entity.Transform;

				UE_LOG(LogSeinSquadSystem, Verbose,
					TEXT("[SquadInit] %s: starting cascade. SlotCount=%d, SquadLoc=%s, Owner=%s"),
					*Handle.ToString(), Squad->Slots.Num(),
					*SquadXform.GetLocation().ToString(), *OwnerPlayer.ToString());

				// Resolver class resolution — priority order:
				//   1. Per-squad explicit class (designer authored their own
				//      resolver subclass and pointed the squad at it via
				//      FSeinSquadComponent::DispatchResolverClass). Wins
				//      outright when set and non-abstract.
				//   2. Project-wide default (USeinARTSSquadSettings::
				//      DefaultSquadDispatchResolverClass, soft path). Empty by
				//      default → falls through to (3). Projects wanting cover-
				//      aware squad dispatch out of the box point it at the Cover
				//      Extension's bridge resolver; resolves to null (→ fallback)
				//      when that module isn't present.
				//   3. Framework default `USeinSquadDispatchResolver` — plain
				//      per-slot formation, no cover-snap.
				TSubclassOf<USeinCommandBrokerResolver> ResolverClass = Squad->DispatchResolverClass;
				if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
				{
					if (const USeinARTSSquadSettings* SquadSettings = GetDefault<USeinARTSSquadSettings>())
					{
						if (!SquadSettings->DefaultSquadDispatchResolverClass.IsNull())
						{
							// LoadSynchronous returns null when the soft path's
							// module isn't loaded (e.g. cover stripped) — the
							// fallback below picks that case up.
							ResolverClass = SquadSettings->DefaultSquadDispatchResolverClass.LoadSynchronous();
						}
					}
				}
				if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
				{
					ResolverClass = USeinSquadDispatchResolver::StaticClass();
				}
				UE_LOG(LogSeinSquadSystem, Verbose,
					TEXT("[SquadInit] %s: resolver=%s (per-squad=%s)"),
					*Handle.ToString(), *GetNameSafe(ResolverClass),
					*GetNameSafe(Squad->DispatchResolverClass.Get()));

				// Build + attach the persistent broker. bSelfCullOnEmpty=false
				// so the broker system's empty-list cull skips this broker —
				// THIS system (FSeinSquadSystem) owns the squad entity's
				// lifetime via step 7's cull check.
				FSeinCommandBrokerData NewBroker;
				NewBroker.bSelfCullOnEmpty = false;
				NewBroker.Centroid = SquadXform.GetLocation();
				NewBroker.Anchor = SquadXform.GetLocation();
				NewBroker.bCapabilityMapDirty = true;
				NewBroker.bAvoidAsCohesiveBody = Squad->bAvoidAsBlob;   // obstacle-side blob opt-in
				NewBroker.bPaceSquadsTogether =                        // outer squad-cohesion opt-in (squad setting)
					GetDefault<USeinARTSSquadSettings>() ? GetDefault<USeinARTSSquadSettings>()->bPaceSquadsTogether : true;
				{
					USeinCommandBrokerResolver* ResolverInstance =
						NewObject<USeinCommandBrokerResolver>(&World, ResolverClass);
					NewBroker.ResolverID = World.RegisterCommandBrokerResolver(ResolverInstance);
				}
				World.AddComponent(Handle, NewBroker);

				// Re-fetch — AddComponent may have relocated component storage.
				Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
				if (!Broker) return;       // unrecoverable; bail this tick

				// Spawn each slot's member entity. Skips slots whose Entity is
				// null/abstract (those represent "empty starting slot, fill via
				// reinforce") and slots whose Entity matches the squad's own
				// actor class (recursion guard against squad-of-squads BPs).
				const UClass* SquadActorClass = nullptr;
				if (const TSubclassOf<ASeinActor> ActorClass = World.GetEntityActorClass(Handle))
				{
					SquadActorClass = *ActorClass;
				}

				int32 SpawnedCount = 0;
				int32 SkippedNullEntity = 0;
				int32 SkippedRecursion = 0;
				int32 SkippedSpawnFail = 0;
				for (int32 SlotIdx = 0; SlotIdx < Squad->Slots.Num(); ++SlotIdx)
				{
					FSeinSquadSlot& Slot = Squad->Slots[SlotIdx];

					if (!Slot.Entity || Slot.Entity->HasAnyClassFlags(CLASS_Abstract)) { ++SkippedNullEntity; continue; }
					if (SquadActorClass && Slot.Entity.Get() == SquadActorClass) { ++SkippedRecursion; continue; }

					// Slot offsets are POSITIONAL anchors — compose location +
					// rotation only, never scale. A slot's authored Scale must not
					// propagate into the member's sim transform: the bridge drives
					// the actor's render scale from it, so a degenerate authored
					// scale (e.g. the zeroed FFixedPoint values from the fix-1
					// serializer window) yields invisible-but-functional members.
					FFixedTransform MemberXform = SquadXform * Slot.OffsetTransform;
					MemberXform.Scale = FFixedVector::Identity;
					const FSeinEntityHandle Member = World.SpawnEntity(Slot.Entity, MemberXform, OwnerPlayer);
					if (!Member.IsValid()) { ++SkippedSpawnFail; continue; }
					++SpawnedCount;

					// Resolve slot's discriminator tag (first tag in container).
					FGameplayTag SlotTag;
					for (const FGameplayTag& Tag : Slot.SlotTags) { SlotTag = Tag; break; }

					// Wire FSeinSquadMemberComponent (overwrite any AC-injected default).
					if (FSeinSquadMemberComponent* ExistingMember = World.GetComponent<FSeinSquadMemberComponent>(Member))
					{
						ExistingMember->SquadEntity = Handle;
						ExistingMember->SlotIndex = SlotIdx;
						ExistingMember->SlotTag = SlotTag;
					}
					else
					{
						FSeinSquadMemberComponent NewMember;
						NewMember.SquadEntity = Handle;
						NewMember.SlotIndex = SlotIdx;
						NewMember.SlotTag = SlotTag;
						World.AddComponent(Member, NewMember);
					}

					// Wire FSeinBrokerMembershipData (back-ref to the squad broker = this squad).
					if (FSeinBrokerMembershipData* ExistingMemb = World.GetComponent<FSeinBrokerMembershipData>(Member))
					{
						ExistingMemb->CurrentBrokerHandle = Handle;
					}
					else
					{
						FSeinBrokerMembershipData NewMemb;
						NewMemb.CurrentBrokerHandle = Handle;
						World.AddComponent(Member, NewMemb);
					}

					// Re-fetch broker pointer (storage may have moved during AddComponent above).
					Broker = World.GetComponent<FSeinCommandBrokerData>(Handle);
					if (!Broker) return;

					Slot.CurrentOccupant = Member;
					Broker->Members.AddUnique(Member);

					World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadMemberAddedEvent(Handle, Member, SlotTag));
				}

				Broker->bCapabilityMapDirty = true;

				// Promote first live occupant as Leader.
				if (!Squad->Leader.IsValid())
				{
					for (const FSeinSquadSlot& Slot : Squad->Slots)
					{
						if (Slot.CurrentOccupant.IsValid())
						{
							Squad->Leader = Slot.CurrentOccupant;
							World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadLeaderChangedEvent(Handle, Squad->Leader));
							break;
						}
					}
				}

				// Initial formation pass: when the squad authored a non-default formation, lay the
				// freshly-spawned members out with it (the SAME resolver entry point dispatch + preview
				// use) instead of leaving them at their authored slot offsets. EMPTY FormationClass = the
				// slot formation, for which the slot-offset spawn above is already correct -> skip.
				if (!Squad->FormationClass.IsNull())
				{
					if (USeinCommandBrokerResolver* SquadResolver = World.GetCommandBrokerResolver(Broker->ResolverID))
					{
						const TArray<FSeinEntityHandle> LiveMembers = Squad->GetLiveMembers();
						if (LiveMembers.Num() > 0)
						{
							FSeinOrderTarget InitTarget;
							InitTarget.Anchor          = SquadXform.GetLocation();
							InitTarget.CurrentCentroid = SquadXform.GetLocation();
							InitTarget.CurrentFacing   = SquadXform.GetQuaternionRotation();
							InitTarget.FormationClass  = Squad->FormationClass;
							const FSeinFormationLayout InitLayout = SquadResolver->ResolveFormationLayout(
								&World, LiveMembers, InitTarget,
								Squad->bReassignSlotsLateral, Squad->bReassignSlotsDepth);
							for (int32 MemberIdx = 0; MemberIdx < LiveMembers.Num(); ++MemberIdx)
							{
								if (!InitLayout.Positions.IsValidIndex(MemberIdx)) continue;
								if (FSeinEntity* MemberEnt = World.GetEntity(LiveMembers[MemberIdx]))
								{
									MemberEnt->Transform.SetLocation(InitLayout.Positions[MemberIdx]);
									if (InitLayout.Facings.IsValidIndex(MemberIdx))
									{
										MemberEnt->Transform.SetRotation(InitLayout.Facings[MemberIdx]);
									}
								}
							}
						}
					}
				}

				UE_LOG(LogSeinSquadSystem, Verbose,
					TEXT("[SquadInit] %s: complete. Spawned=%d, SkippedNullEntity=%d, SkippedRecursion=%d, SkippedSpawnFail=%d, Leader=%s, BrokerMembers=%d"),
					*Handle.ToString(), SpawnedCount, SkippedNullEntity, SkippedRecursion, SkippedSpawnFail,
					*Squad->Leader.ToString(), Broker ? Broker->Members.Num() : -1);

				// Fall through to the rest of the tick — strip/promote/etc. all
				// no-op for a freshly-initialized squad (members are live, leader
				// is set, no reinforce queue). Cull check at the end sees a
				// non-empty squad and leaves it alone.
			}

			// 1. Strip dead occupants from slots; emit SquadMemberDied.
			for (FSeinSquadSlot& Slot : Squad->Slots)
			{
				if (!Slot.CurrentOccupant.IsValid()) { continue; }
				if (World.GetEntityPool().IsValid(Slot.CurrentOccupant)) { continue; }

				// Member entity is gone — strip the slot, emit event, drop from broker.
				const FSeinEntityHandle Dead = Slot.CurrentOccupant;
				FGameplayTag SlotTag;
				for (const FGameplayTag& Tag : Slot.SlotTags) { SlotTag = Tag; break; }
				UE_LOG(LogSeinSquadSystem, Log,
					TEXT("[SquadStrip] %s: member %s (slot tag=%s) is no longer alive in pool — stripping"),
					*Handle.ToString(), *Dead.ToString(), *SlotTag.ToString());
				World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadMemberDiedEvent(Handle, Dead, SlotTag));
				Slot.CurrentOccupant = FSeinEntityHandle::Invalid();

				if (Broker)
				{
					const int32 NumBefore = Broker->Members.Num();
					Broker->Members.Remove(Dead);
					if (Broker->Members.Num() != NumBefore) { Broker->bCapabilityMapDirty = true; }
				}
			}

			// 2. Promote a new leader if the current one died (or was never set).
			const bool bLeaderAlive = Squad->Leader.IsValid()
				&& World.GetEntityPool().IsValid(Squad->Leader)
				&& Squad->IndexOfSlotByMember(Squad->Leader) != INDEX_NONE;
			if (!bLeaderAlive)
			{
				FSeinEntityHandle NewLeader = FSeinEntityHandle::Invalid();
				for (const FSeinSquadSlot& Slot : Squad->Slots)
				{
					if (Slot.CurrentOccupant.IsValid())
					{
						NewLeader = Slot.CurrentOccupant;
						break;
					}
				}
				if (NewLeader != Squad->Leader)
				{
					Squad->Leader = NewLeader;
					World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadLeaderChangedEvent(Handle, NewLeader));
				}
			}

			// 3 + 4. Centroid update — sum live member positions, divide. Write to
			// the squad entity's transform so render-side banner widgets track,
			// and to the broker's centroid/anchor for resolver inputs.
			{
				FFixedVector Sum = FFixedVector::ZeroVector;
				int32 Count = 0;
				for (const FSeinSquadSlot& Slot : Squad->Slots)
				{
					if (!Slot.CurrentOccupant.IsValid()) { continue; }
					if (const FSeinEntity* MemberEntity = World.GetEntity(Slot.CurrentOccupant))
					{
						Sum = Sum + MemberEntity->Transform.GetLocation();
						++Count;
					}
				}
				if (Count > 0)
				{
					const FFixedVector Centroid = Sum / FFixedPoint::FromInt(Count);
					Entity.Transform.Location = Centroid;
					if (Broker)
					{
						Broker->Centroid = Centroid;
						// Anchor only re-syncs when the queue is empty — once any
						// order is queued (executing or not under the per-order
						// parallelism model), the broker's anchor is the most-
						// recently dispatched order's target and shouldn't drift
						// with member centroid recomputation.
						if (Broker->OrderQueue.Num() == 0)
						{
							Broker->Anchor = Centroid;
						}
					}
				}
			}

			// 4b. Update FormationWidth (lateral extent of slot offsets) + FormationRadius (bounding
				// circle inclusive of member footprints). ProcessCommands / the parent formation read
				// these to place the whole squad as ONE footprint-sized element among other units.
				if (Broker)
				{
					FFixedPoint MinY = FFixedPoint::Zero;
					FFixedPoint MaxY = FFixedPoint::Zero;
					bool bAnySlot = false;
					for (const FSeinSquadSlot& Slot : Squad->Slots)
					{
						const FFixedPoint Y = Slot.OffsetTransform.GetLocation().Y;
						if (!bAnySlot) { MinY = Y; MaxY = Y; bAnySlot = true; }
						else
						{
							if (Y < MinY) { MinY = Y; }
							if (Y > MaxY) { MaxY = Y; }
						}
					}
					Broker->FormationWidth = MaxY - MinY;

					// FormationRadius: the farthest member EDGE from the squad's placement origin — for
					// each live member, its slot-offset distance (XY) PLUS its own footprint radius, taken
					// as the max. Inclusive of the full footprint of all squad members, so the parent
					// formation never overlaps a squad with its neighbours. Measured from the origin (the
					// frame USeinSlotFormation places offsets in) so the bound matches actual placement.
					FFixedPoint MaxRadius = FFixedPoint::Zero;
					if (Squad->FormationClass.IsNull())
					{
						// Slot formation (default): footprint = farthest authored slot offset + member footprint.
						for (const FSeinEntityHandle& Member : Squad->GetLiveMembers())
						{
							FFixedVector Offset = FFixedVector::ZeroVector;
							if (const FSeinSquadMemberComponent* MemberData = World.GetComponent<FSeinSquadMemberComponent>(Member))
							{
								if (Squad->Slots.IsValidIndex(MemberData->SlotIndex))
								{
									Offset = Squad->Slots[MemberData->SlotIndex].OffsetTransform.GetLocation();
								}
							}
							const FFixedVector OffsetXY(Offset.X, Offset.Y, FFixedPoint::Zero);
							const FFixedPoint Reach = OffsetXY.Size() + USeinFormation::GetFootprintRadius(&World, Member);
							if (Reach > MaxRadius) { MaxRadius = Reach; }
						}
					}
					else
					{
						// Non-slot formation: the authored slots DON'T describe the layout. Dry-run the chosen
						// formation over the live members at a neutral origin / identity facing and take
						// max(|pos| + member footprint), so the parent formation sizes the squad by where its
						// members ACTUALLY go (else white space when slots are wider than the shape, overlap when
						// narrower). Rotation-invariant (a radius), so identity facing is fine. Deterministic.
						const TArray<FSeinEntityHandle> LiveMembers = Squad->GetLiveMembers();
						USeinFormation* FpFormation = nullptr;
						if (UClass* FpClass = Squad->FormationClass.LoadSynchronous())
						{
							if (!FpClass->HasAnyClassFlags(CLASS_Abstract)) { FpFormation = GetMutableDefault<USeinFormation>(FpClass); }
						}
						if (FpFormation && LiveMembers.Num() > 0)
						{
							FSeinOrderTarget FpTarget;
							FpTarget.Anchor          = FFixedVector::ZeroVector;
							FpTarget.CurrentCentroid = FFixedVector::ZeroVector;
							FpTarget.CurrentFacing   = FFixedQuaternion::Identity;
							const FSeinFormationLayout FpLayout = FpFormation->BuildFormation(&World, LiveMembers, FpTarget);
							for (int32 m = 0; m < LiveMembers.Num(); ++m)
							{
								const FFixedVector P = FpLayout.Positions.IsValidIndex(m) ? FpLayout.Positions[m] : FFixedVector::ZeroVector;
								const FFixedVector PXY(P.X, P.Y, FFixedPoint::Zero);
								const FFixedPoint Reach = PXY.Size() + USeinFormation::GetFootprintRadius(&World, LiveMembers[m]);
								if (Reach > MaxRadius) { MaxRadius = Reach; }
							}
						}
					}
					Broker->FormationRadius = MaxRadius;
				}

				// Keep the broker's obstacle-side blob flag live with the squad authoring (a
				// runtime toggle takes effect next tick — the avoidance kernel reads it PreTick).
				Broker->bAvoidAsCohesiveBody = Squad->bAvoidAsBlob;
				// Keep the outer squad-cohesion flag live with the squad setting.
				Broker->bPaceSquadsTogether =
					GetDefault<USeinARTSSquadSettings>() ? GetDefault<USeinARTSSquadSettings>()->bPaceSquadsTogether : true;

				// 5. Per-slot cooldown decrement (toward zero).
			for (FSeinSquadSlot& Slot : Squad->Slots)
			{
				if (Slot.CurrentCooldown > FFixedPoint::Zero)
				{
					Slot.CurrentCooldown = Slot.CurrentCooldown - DeltaTime;
					if (Slot.CurrentCooldown < FFixedPoint::Zero)
					{
						Slot.CurrentCooldown = FFixedPoint::Zero;
					}
				}
			}

			// 6. Reinforce queue tick — front entry only (others wait their turn).
			if (Squad->ReinforceQueue.Num() > 0)
			{
				FSeinSquadReinforceEntry& Front = Squad->ReinforceQueue[0];
				Front.BuildProgress = Front.BuildProgress + DeltaTime;

				if (Front.BuildProgress >= Front.TotalBuildTime)
				{
					// Resolve target slot. If it's been refilled meanwhile (designer
					// flow), the entry silently drops without spawning — no double-fill.
					const int32 SlotIdx = Squad->IndexOfSlotByTag(Front.SlotTag);
					if (SlotIdx != INDEX_NONE)
					{
						FSeinSquadSlot& Slot = Squad->Slots[SlotIdx];
						if (!Slot.CurrentOccupant.IsValid() && Slot.Entity && !Slot.Entity->HasAnyClassFlags(CLASS_Abstract))
						{
							const FSeinPlayerID OwnerPlayer = World.GetEntityOwner(Handle);
							const FFixedTransform SquadXform = Entity.Transform;
							// Positional anchor only — see the lazy-init spawn above.
							FFixedTransform MemberXform = SquadXform * Slot.OffsetTransform;
							MemberXform.Scale = FFixedVector::Identity;

							const FSeinEntityHandle NewMember = World.SpawnEntity(Slot.Entity, MemberXform, OwnerPlayer);
							if (NewMember.IsValid())
							{
								// Wire member back-refs (overwrite any AC-injected defaults).
								// SlotIndex is canonical (always unique per array position);
								// SlotTag is role metadata that may be shared across slots.
								// Resolvers prefer SlotIndex for formation lookup.
								if (FSeinSquadMemberComponent* MemberData = World.GetComponent<FSeinSquadMemberComponent>(NewMember))
								{
									MemberData->SquadEntity = Handle;
									MemberData->SlotIndex = SlotIdx;
									MemberData->SlotTag = Front.SlotTag;
								}
								else
								{
									FSeinSquadMemberComponent NewData;
									NewData.SquadEntity = Handle;
									NewData.SlotIndex = SlotIdx;
									NewData.SlotTag = Front.SlotTag;
									World.AddComponent(NewMember, NewData);
								}
								if (FSeinBrokerMembershipData* MembData = World.GetComponent<FSeinBrokerMembershipData>(NewMember))
								{
									MembData->CurrentBrokerHandle = Handle;
								}
								else
								{
									FSeinBrokerMembershipData NewMemb;
									NewMemb.CurrentBrokerHandle = Handle;
									World.AddComponent(NewMember, NewMemb);
								}

								Slot.CurrentOccupant = NewMember;
								Slot.CurrentCooldown = Slot.ReinforceCooldown;

								// Re-fetch broker pointer (storage may have moved during AddComponent).
								if (FSeinCommandBrokerData* BrokerAfter = World.GetComponent<FSeinCommandBrokerData>(Handle))
								{
									BrokerAfter->Members.AddUnique(NewMember);
									BrokerAfter->bCapabilityMapDirty = true;
								}

								World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadMemberAddedEvent(Handle, NewMember, Front.SlotTag));

								// Promote leader if the squad had none (first reinforce on a
								// fully-emptied squad re-establishes leadership).
								if (!Squad->Leader.IsValid())
								{
									Squad->Leader = NewMember;
									World.EnqueueVisualEvent(FSeinVisualEvent::MakeSquadLeaderChangedEvent(Handle, NewMember));
								}
							}
						}
					}
					Squad->ReinforceQueue.RemoveAt(0);
				}
			}

			// 7. Cull check.
			const bool bAllSlotsEmpty = (Squad->GetLiveMemberCount() == 0);
			const bool bReinforceIdle = (Squad->ReinforceQueue.Num() == 0);
			// Per-order parallelism: empty queue = nothing executing. No
			// separate broker-level flag to consult.
			const bool bBrokerIdle = !Broker || Broker->OrderQueue.Num() == 0;
			if (bAllSlotsEmpty && bReinforceIdle && bBrokerIdle)
			{
				// Log (not Warning) — squad cull is the normal end-of-life path:
				// last member died OR a designer destroyed the squad entity
				// explicitly. Promote back to Warning + enable
				// LogSeinSquadSystem Verbose for the per-slot detail when
				// diagnosing "squad culled too early" regressions.
				UE_LOG(LogSeinSquadSystem, Log,
					TEXT("[SquadCull] %s: culling — bAllSlotsEmpty=%d bReinforceIdle=%d bBrokerIdle=%d (Broker=%p, SlotCount=%d, LiveMembers=%d)"),
					*Handle.ToString(),
					bAllSlotsEmpty ? 1 : 0, bReinforceIdle ? 1 : 0, bBrokerIdle ? 1 : 0,
					Broker, Squad->Slots.Num(), Squad->GetLiveMemberCount());
				CullList.Add(Handle);
			}
		});

		for (const FSeinEntityHandle& H : CullList)
		{
			World.DestroyEntity(H);
		}

		// Verbose (not Warning) — per-second heartbeat. Originally promoted to
		// Warning while diagnosing the "squad culled at PIE start" regression;
		// that's resolved now, so the heartbeat is back to its normal home at
		// Verbose. Re-enable via `log LogSeinSquadSystem Verbose` if you need
		// to confirm the tick is alive while diagnosing future squad weirdness.
		if (bDiagThisTick)
		{
			UE_LOG(LogSeinSquadSystem, Verbose,
				TEXT("[SquadTick] tick=%d entitiesSeen=%d squadsFound=%d cullList=%d"),
				TickCounter, EntitiesSeen, SquadsFound, CullList.Num());
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::Squad; }
	virtual FName GetSystemName() const override { return TEXT("Squad"); }
};
