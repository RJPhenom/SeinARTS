/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargeterBPFL.cpp
 * @brief   Action-slot dispatcher implementation.
 */

#include "Lib/SeinTargeterBPFL.h"
#include "Player/SeinPlayerController.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Actor/SeinActor.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinSquadComponent.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Abilities/SeinTargeterTypes.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/FixedPoint.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinTargeterBPFL, Log, All);

bool USeinTargeterBPFL::TriggerAbilityFromActionSlot(ASeinPlayerController* PC, FGameplayTag AbilityTag)
{
	if (!PC)
	{
		UE_LOG(LogSeinTargeterBPFL, Verbose, TEXT("Trigger: null PC."));
		return false;
	}
	if (!AbilityTag.IsValid())
	{
		UE_LOG(LogSeinTargeterBPFL, Verbose, TEXT("Trigger: invalid ability tag."));
		return false;
	}

	USeinWorldSubsystem* World = PC->GetWorldSubsystem();
	if (!World)
	{
		UE_LOG(LogSeinTargeterBPFL, Verbose, TEXT("Trigger: no world subsystem."));
		return false;
	}

	// Pick the leader: prefer focused unit IF it has the ability granted; else
	// scan all selected units for the first one with this ability tag. This
	// matters for heterogeneous selections (e.g. 4 riflemen + 1 grenadier with
	// smoke) — old logic only checked the leader, so the grenadier's smoke
	// would silently fail when the rifleman was leader. The dynamic action
	// panel (future) shows abilities aggregated across the selection, so any
	// shown ability must be invokable regardless of which member has it.
	ASeinActor* LeaderActor = nullptr;
	USeinAbility* Ability = nullptr;

	auto TryResolveOnActor = [&](ASeinActor* Candidate) -> bool
	{
		if (!Candidate || !Candidate->HasValidEntity()) return false;
		const FSeinEntityHandle CandidateHandle = Candidate->GetEntityHandle();

		// First: the actor's own AC. Original single-entity behavior — works
		// for individual units and squad-owned abilities (the squad's own
		// FSeinAbilityComponent).
		if (const FSeinAbilityComponent* AC = World->GetComponent<FSeinAbilityComponent>(CandidateHandle))
		{
			if (USeinAbility* Found = AC->FindAbilityByTag(*World, AbilityTag))
			{
				LeaderActor = Candidate;
				Ability = Found;
				return true;
			}
		}

		// Squad-aware fallback: if the selected actor is a squad, walk each
		// live member's AC for the ability tag. The squad entity itself
		// rarely carries every ability — most abilities live on the members
		// (riflemen grant Move/Attack, sergeant grants Smoke, MG grants
		// Suppress, etc.). The view-model aggregates these for the ability
		// bar; this branch makes the trigger path see them too.
		//
		// LEADER ROUTING: when found via a member, we leave LeaderActor as
		// the SQUAD actor (not the member). The downstream
		// `PC->IssueTargetedAbility(AbilityTag, Leader, ...)` call uses the
		// leader's entity handle to address the broker order — and the
		// broker that resolves the dispatch lives on the squad entity, not
		// the member. Pointing Leader at the member would bypass the
		// squad's broker and lose the dispatch policy. The Ability
		// reference (used downstream for TargetType / TargeterSpec) comes
		// from the member's instance, which is fine — all member instances
		// of the same ability class have identical class-level metadata.
		if (const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(CandidateHandle))
		{
			for (const FSeinSquadSlot& Slot : SquadData->Slots)
			{
				if (!Slot.CurrentOccupant.IsValid()) continue;
				const FSeinAbilityComponent* MemberAC = World->GetComponent<FSeinAbilityComponent>(Slot.CurrentOccupant);
				if (!MemberAC) continue;
				if (USeinAbility* Found = MemberAC->FindAbilityByTag(*World, AbilityTag))
				{
					LeaderActor = Candidate;     // squad actor — broker dispatch carrier
					Ability = Found;             // member's instance — for TargetType / Spec
					return true;
				}
			}
		}
		return false;
	};

	// Step 1: focused unit gets first dibs (matches "user explicitly Tab-cycled
	// to this unit" intent), but only if it actually has the ability.
	if (PC->ActiveFocusIndex >= 0 && PC->ActiveFocusIndex < PC->SelectedActors.Num())
	{
		TryResolveOnActor(PC->SelectedActors[PC->ActiveFocusIndex].Get());
	}

	// Step 2: fall through to scanning the full selection for the first
	// ability-capable unit. Order matches SelectedActors (selection order).
	if (!Ability)
	{
		for (const TWeakObjectPtr<ASeinActor>& Weak : PC->SelectedActors)
		{
			if (TryResolveOnActor(Weak.Get())) break;
		}
	}

	if (!LeaderActor || !Ability)
	{
		UE_LOG(LogSeinTargeterBPFL, Verbose,
			TEXT("Trigger %s: no selected unit has this ability granted."), *AbilityTag.ToString());
		return false;
	}

	const FSeinEntityHandle Leader = LeaderActor->GetEntityHandle();

	// Branch on TargetType. Self / None / Passive fire immediately — no targeter
	// involvement. Entity / Point / Area enter the targeter capture flow.
	const ESeinAbilityTargetType TargetType = Ability->TargetType;
	const bool bNeedsTargeter =
		TargetType == ESeinAbilityTargetType::Entity ||
		TargetType == ESeinAbilityTargetType::Point ||
		TargetType == ESeinAbilityTargetType::Area;

	if (!bNeedsTargeter)
	{
		// Immediate fire. PC->IssueTargetedAbility handles empty-Points cleanly
		// for Self/None/Passive (TargetLocation defaults to zero; ability uses
		// owner-relative state in OnActivate).
		UE_LOG(LogSeinTargeterBPFL, Verbose, TEXT("Trigger %s: immediate fire (TargetType=%d)"),
			*AbilityTag.ToString(), (int32)TargetType);
		PC->IssueTargetedAbility(AbilityTag, Leader, /*Points=*/{});
		return true;
	}

	// Need targeter. Ability must declare a spec OR we'd need a default —
	// Phase 1 requires designers to attach a USeinPointTargeterSpec explicitly.
	// Future enhancement: synthesize a default Point spec from TargetType when
	// TargeterSpec is null, so trivial Point/Area abilities don't need a spec
	// asset. For now we log and skip if missing — clearer authoring error.
	USeinTargeterSpec* Spec = Ability->TargeterSpec;
	if (!Spec)
	{
		UE_LOG(LogSeinTargeterBPFL, Warning,
			TEXT("Trigger %s: TargetType requires a targeter but TargeterSpec is null. ")
			TEXT("Author a USeinPointTargeterSpec on the ability's TargeterSpec field."),
			*AbilityTag.ToString());
		return false;
	}

	USeinTargeterSubsystem* Targeter = PC->GetTargeterSubsystem();
	if (!Targeter)
	{
		UE_LOG(LogSeinTargeterBPFL, Warning,
			TEXT("Trigger %s: no targeter subsystem (dedicated server / no local player)."),
			*AbilityTag.ToString());
		return false;
	}

	const float AreaRadiusWorld = Ability->AreaRadius.ToFloat();
	Targeter->Activate(Spec, AbilityTag, Leader, AreaRadiusWorld);
	UE_LOG(LogSeinTargeterBPFL, Verbose, TEXT("Trigger %s: targeter activated."),
		*AbilityTag.ToString());
	return true;
}

FFixedTransform USeinTargeterBPFL::GetTargeterPointTransform(const FSeinTargeterPoint& Point)
{
	// Compose the captured point into a deterministic FFixedTransform. Yaw lives
	// on Point.YawDegrees (already snapped or free per the spec's
	// RotationStepDegrees); no math required by the caller. Stays in fixed-point
	// throughout — designer converts to FTransform at the UE-API boundary
	// (e.g. just before Spawn Actor From Class) via FFixedTransform's
	// ToTransform method.
	const FFixedRotator Rot(FFixedPoint::Zero, Point.YawDegrees, FFixedPoint::Zero);
	return FFixedTransform(Point.Location, Rot);
}

FFixedRotator USeinTargeterBPFL::GetTargeterPointRotation(const FSeinTargeterPoint& Point)
{
	return FFixedRotator(FFixedPoint::Zero, Point.YawDegrees, FFixedPoint::Zero);
}

FFixedVector USeinTargeterBPFL::GetTargeterPointLocation(const FSeinTargeterPoint& Point)
{
	return Point.Location;
}
