/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityViewModel.cpp
 * @brief   Entity ViewModel implementation.
 */

#include "ViewModel/SeinEntityViewModel.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinViewModel, Log, All);
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinIdentityComponent.h"
#include "Components/SeinProductionComponent.h"
#include "Components/SeinSquadComponent.h"
#include "Core/SeinPlayerState.h"
#include "Effects/SeinEffect.h"
#include "Lib/SeinAbilityBPFL.h"
#include "StructUtils/InstancedStruct.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"

namespace SeinEntityViewModelLocal
{
	/** (Ability*, OwningEntityHandle) pair so BuildAbilityInfo can run its
	 *  availability check against the entity that ACTUALLY owns the ability
	 *  instance — not the squad selection handle. Without this, all
	 *  member-owned abilities fail the squad-side availability check
	 *  (squad doesn't own them) and the UI renders them as disabled/hidden. */
	struct FSquadAbilityEntry
	{
		USeinAbility* Ability = nullptr;
		FSeinEntityHandle Owner;
	};

	/** Collect EVERY ability instance the squad presents to the player —
	 *  the union of (squad-owned + each live member's) instances, with NO
	 *  dedup. Caller groups by tag and merges via MergeAbilityInfos so the
	 *  resulting per-tag UI entry has OR-availability and MIN-cooldown
	 *  across all owners (matching the rule for selections — the same
	 *  composition math).
	 *
	 *  Squad-owned instances come first (added before member walks) so
	 *  they lead a tag's instance list when both squad and members hold
	 *  the same tag. Member instances follow in slot-declaration order.
	 *  This preserves UI presentation order (squad-owned abilities listed
	 *  first; member-only abilities follow in slot order). */
	static TArray<FSquadAbilityEntry> GatherSquadAbilities(const USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		const FSeinSquadComponent& SquadData)
	{
		TArray<FSquadAbilityEntry> Out;

		// Pass 1: squad-owned instances.
		if (const FSeinAbilityComponent* SquadAC = World.GetComponent<FSeinAbilityComponent>(SquadHandle))
		{
			for (USeinAbility* Ab : SquadAC->GetAbilityInstances(World))
			{
				if (Ab && Ab->AbilityTag.IsValid())
				{
					Out.Add({Ab, SquadHandle});
				}
			}
		}

		// Pass 2: each member's instances in slot order.
		for (const FSeinSquadSlot& Slot : SquadData.Slots)
		{
			if (!Slot.CurrentOccupant.IsValid()) continue;
			const FSeinAbilityComponent* MemberAC = World.GetComponent<FSeinAbilityComponent>(Slot.CurrentOccupant);
			if (!MemberAC) continue;
			for (USeinAbility* Ab : MemberAC->GetAbilityInstances(World))
			{
				if (Ab && Ab->AbilityTag.IsValid())
				{
					Out.Add({Ab, Slot.CurrentOccupant});
				}
			}
		}

		return Out;
	}

	/** Collect every (Ability*, Owner) instance of `Tag` across the squad's
	 *  entity + members. Caller merges via MergeAbilityInfos to get the
	 *  OR-availability / MIN-cooldown aggregated info. Empty result =
	 *  no squad/member holds the tag.
	 *
	 *  Replaces the historic "find first instance, squad-owned wins"
	 *  behavior — now every instance contributes to the merged result.
	 *  HasAbilityWithTag still gets a `!result.IsEmpty()` check. */
	static TArray<FSquadAbilityEntry> FindSquadAbilityInstancesByTag(
		const USeinWorldSubsystem& World,
		FSeinEntityHandle SquadHandle,
		const FSeinSquadComponent& SquadData,
		FGameplayTag Tag)
	{
		TArray<FSquadAbilityEntry> Out;
		if (!Tag.IsValid()) return Out;
		if (const FSeinAbilityComponent* SquadAC = World.GetComponent<FSeinAbilityComponent>(SquadHandle))
		{
			if (USeinAbility* Ab = SquadAC->FindAbilityByTag(World, Tag))
			{
				Out.Add({Ab, SquadHandle});
			}
		}
		for (const FSeinSquadSlot& Slot : SquadData.Slots)
		{
			if (!Slot.CurrentOccupant.IsValid()) continue;
			const FSeinAbilityComponent* MemberAC = World.GetComponent<FSeinAbilityComponent>(Slot.CurrentOccupant);
			if (!MemberAC) continue;
			if (USeinAbility* Ab = MemberAC->FindAbilityByTag(World, Tag))
			{
				Out.Add({Ab, Slot.CurrentOccupant});
			}
		}
		return Out;
	}
}

void USeinEntityViewModel::Initialize(FSeinEntityHandle InHandle, USeinWorldSubsystem* InWorldSubsystem)
{
	Entity = InHandle;
	WorldSubsystem = InWorldSubsystem;

	if (!InWorldSubsystem || !InHandle.IsValid())
	{
		bIsAlive = false;
		return;
	}

	bIsAlive = InWorldSubsystem->IsEntityAlive(InHandle);
	OwnerPlayerID = InWorldSubsystem->GetEntityOwner(InHandle);

	// Cache identity data straight from sim storage. FSeinIdentityComponent
	// is injected into entity storage at spawn from the entity bridge's
	// authored ComponentData array — no actor lookup needed.
	if (const FSeinIdentityComponent* Identity = InWorldSubsystem->GetComponent<FSeinIdentityComponent>(InHandle))
	{
		DisplayName = Identity->DisplayName;
		Description = Identity->Description;
		Icon = Identity->Icon;
		Portrait = Identity->Portrait;
		IdentityTag = Identity->IdentityTag;
	}
}

void USeinEntityViewModel::Refresh()
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return;
	}

	bIsAlive = WorldSubsystem->IsEntityAlive(Entity);
	if (!bIsAlive)
	{
		Invalidate();
		return;
	}

	OwnerPlayerID = WorldSubsystem->GetEntityOwner(Entity);

	OnRefreshed.Broadcast();
}

void USeinEntityViewModel::Invalidate()
{
	bIsAlive = false;
	OnInvalidated.Broadcast();
}

// ==================== Generic Data Access ====================

float USeinEntityViewModel::GetResolvedAttribute(UScriptStruct* ComponentType, FName FieldName) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return 0.0f;
	}

	FFixedPoint Resolved = WorldSubsystem->ResolveAttribute(Entity, ComponentType, FieldName);
	return Resolved.ToFloat();
}

float USeinEntityViewModel::GetBaseAttribute(UScriptStruct* ComponentType, FName FieldName) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return 0.0f;
	}

	const ISeinComponentStorage* Storage =
		WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return 0.0f;
	}

	const void* CompData = Storage->GetComponentRaw(Entity);
	if (!CompData)
	{
		return 0.0f;
	}

	// Resolve the field via reflection. Supports FFixedPoint, plus plain numeric /
	// bool / byte / enum fields so a custom component's scalar fields surface in UI
	// without going through GetComponentData. (Non-scalar fields — FName, strings,
	// vectors — return 0; read those via GetComponentData instead.)
	FProperty* Prop = ComponentType->FindPropertyByName(FieldName);
	if (!Prop)
	{
		return 0.0f;
	}

	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct == FFixedPoint::StaticStruct())
		{
			const FFixedPoint* Value = StructProp->ContainerPtrToValuePtr<FFixedPoint>(CompData);
			return Value ? Value->ToFloat() : 0.0f;
		}
		return 0.0f;
	}

	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		return BoolProp->GetPropertyValue_InContainer(CompData) ? 1.0f : 0.0f;
	}

	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		const void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(CompData);
		return static_cast<float>(EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr));
	}

	if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		const void* ValuePtr = NumProp->ContainerPtrToValuePtr<void>(CompData);
		return NumProp->IsFloatingPoint()
			? static_cast<float>(NumProp->GetFloatingPointPropertyValue(ValuePtr))
			: static_cast<float>(NumProp->GetSignedIntPropertyValue(ValuePtr));
	}

	return 0.0f;
}

bool USeinEntityViewModel::HasComponent(UScriptStruct* ComponentType) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return false;
	}

	const ISeinComponentStorage* Storage = WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return false;
	}

	return Storage->HasComponent(Entity);
}

FInstancedStruct USeinEntityViewModel::GetComponentData(UScriptStruct* ComponentType) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid() || !ComponentType)
	{
		return FInstancedStruct();
	}

	const ISeinComponentStorage* Storage = WorldSubsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage)
	{
		return FInstancedStruct();
	}

	const void* CompData = Storage->GetComponentRaw(Entity);
	if (!CompData)
	{
		return FInstancedStruct();
	}

	FInstancedStruct Result;
	Result.InitializeAs(ComponentType, static_cast<const uint8*>(CompData));
	return Result;
}

FGameplayTagContainer USeinEntityViewModel::GetTags() const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return FGameplayTagContainer();
	}

	// Returns a copy of the entity's combined tags. World subsystem returns
	// a const ref to its centralized tag state (or an empty container if the
	// entity has no tag state). View model copies to break the lifetime
	// coupling (UI bindings hold the result independently).
	return WorldSubsystem->GetEntityTags(Entity);
}

// ==================== Relationship ====================

ESeinRelation USeinEntityViewModel::GetRelationToPlayer(FSeinPlayerID PlayerID) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return ESeinRelation::Neutral;
	}

	if (OwnerPlayerID.IsNeutral())
	{
		return ESeinRelation::Neutral;
	}

	if (OwnerPlayerID == PlayerID)
	{
		return ESeinRelation::Friendly;
	}

	if (WorldSubsystem->ShouldPresentPlayerAsFriendly(
		OwnerPlayerID, PlayerID))
	{
		return ESeinRelation::Friendly;
	}

	return ESeinRelation::Enemy;
}

ESeinRelation USeinEntityViewModel::GetRelationToLocalPlayer() const
{
	UWorld* World = WorldSubsystem.IsValid() ? WorldSubsystem->GetWorld() : nullptr;
	if (!World)
	{
		return ESeinRelation::Neutral;
	}

	ASeinPlayerController* PC = Cast<ASeinPlayerController>(World->GetFirstPlayerController());
	if (!PC)
	{
		return ESeinRelation::Neutral;
	}

	return GetRelationToPlayer(PC->SeinPlayerID);
}

// ==================== Ability Access ====================

TArray<FSeinAbilityInfo> USeinEntityViewModel::GetAbilities() const
{
	TArray<FSeinAbilityInfo> Result;

	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return Result;
	}

	// Squad-aware: when the selected entity is a squad, present the union of
	// (squad's own + each live member's) abilities. For each tag, all
	// holding instances contribute to the merged FSeinAbilityInfo via
	// MergeAbilityInfos — OR-availability and MIN-cooldown across owners.
	// Squad-scope cooldowns naturally produce equal cooldown values across
	// member instances of the same tag, so MIN is a no-op identity there;
	// it only differs for member-scope abilities granted to multiple
	// members independently (rare in squads but supported).
	//
	// Per-tag instance order: squad-owned first (added by Pass 1 of
	// GatherSquadAbilities), then members in slot-declaration order. The
	// first instance for each tag carries the class-level fields that
	// MergeAbilityInfos copies through (Name, Icon, Cost, Cooldown, etc.).
	if (const FSeinSquadComponent* SquadData = WorldSubsystem->GetComponent<FSeinSquadComponent>(Entity))
	{
		const TArray<SeinEntityViewModelLocal::FSquadAbilityEntry> AllInstances =
			SeinEntityViewModelLocal::GatherSquadAbilities(*WorldSubsystem, Entity, *SquadData);

		// Group by tag, preserving first-encounter order so UI presentation
		// stays stable across ticks. A map of FSeinAbilityInfo arrays keyed
		// by tag would suffice for the merge, but we also need a deterministic
		// emit order — TagOrder array gives us that.
		TArray<FGameplayTag> TagOrder;
		TMap<FGameplayTag, TArray<FSeinAbilityInfo>> PerTag;
		for (const SeinEntityViewModelLocal::FSquadAbilityEntry& Entry : AllInstances)
		{
			const FGameplayTag Tag = Entry.Ability->AbilityTag;
			// Defensive: GatherSquadAbilities already filters invalid-tag
			// instances, but re-checking here protects against future code
			// paths that bypass that filter.
			if (!Tag.IsValid()) continue;
			if (!PerTag.Contains(Tag))
			{
				TagOrder.Add(Tag);
			}
			PerTag.FindOrAdd(Tag).Add(BuildAbilityInfo(Entry.Ability, Entry.Owner));
		}

		Result.Reserve(TagOrder.Num());
		for (const FGameplayTag& Tag : TagOrder)
		{
			Result.Add(MergeAbilityInfos(PerTag[Tag]));
		}
		return Result;
	}

	const FSeinAbilityComponent* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityComponent>(Entity);
	if (!AbilityComp)
	{
		return Result;
	}

	// Tag-validity filter: ability instances with `AbilityTag = None` (the
	// default for a freshly-spawned ability whose CDO never set a tag) would
	// otherwise pass through here as a blank-tag FSeinAbilityInfo. The UI
	// then renders an empty button. Tag-less abilities can't be looked up by
	// the dispatch/availability pipeline anyway, so they have no place in
	// the UI list — drop them defensively. Designer-side fix: set
	// `AbilityTag` on the ability BP CDO.
	for (USeinAbility* Ability : AbilityComp->GetAbilityInstances(*WorldSubsystem))
	{
		if (Ability && Ability->AbilityTag.IsValid())
		{
			Result.Add(BuildAbilityInfo(Ability));
		}
	}

	return Result;
}

FSeinAbilityInfo USeinEntityViewModel::GetAbilityByTag(FGameplayTag Tag) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return FSeinAbilityInfo();
	}

	// Squad-aware lookup — walk every squad/member instance of the tag and
	// merge. Same OR-availability / MIN-cooldown rule as GetAbilities so a
	// per-tag query is consistent with what the deduped ability bar shows.
	if (const FSeinSquadComponent* SquadData = WorldSubsystem->GetComponent<FSeinSquadComponent>(Entity))
	{
		const TArray<SeinEntityViewModelLocal::FSquadAbilityEntry> Instances =
			SeinEntityViewModelLocal::FindSquadAbilityInstancesByTag(*WorldSubsystem, Entity, *SquadData, Tag);
		if (Instances.Num() == 0) return FSeinAbilityInfo();

		TArray<FSeinAbilityInfo> Infos;
		Infos.Reserve(Instances.Num());
		for (const SeinEntityViewModelLocal::FSquadAbilityEntry& E : Instances)
		{
			Infos.Add(BuildAbilityInfo(E.Ability, E.Owner));
		}
		return MergeAbilityInfos(Infos);
	}

	const FSeinAbilityComponent* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityComponent>(Entity);
	if (!AbilityComp)
	{
		return FSeinAbilityInfo();
	}

	USeinAbility* Found = AbilityComp->FindAbilityByTag(*WorldSubsystem.Get(), Tag);
	if (!Found)
	{
		return FSeinAbilityInfo();
	}

	return BuildAbilityInfo(Found);
}

bool USeinEntityViewModel::HasAbilityWithTag(FGameplayTag Tag) const
{
	if (!WorldSubsystem.IsValid() || !Entity.IsValid())
	{
		return false;
	}

	// Squad-aware: ANY entity in the squad (squad itself OR any live member)
	// holding the tag → true. Matches GetAbilities visibility rules.
	if (const FSeinSquadComponent* SquadData = WorldSubsystem->GetComponent<FSeinSquadComponent>(Entity))
	{
		return SeinEntityViewModelLocal::FindSquadAbilityInstancesByTag(*WorldSubsystem, Entity, *SquadData, Tag).Num() > 0;
	}

	const FSeinAbilityComponent* AbilityComp = WorldSubsystem->GetComponent<FSeinAbilityComponent>(Entity);
	return AbilityComp && AbilityComp->HasAbilityWithTag(*WorldSubsystem.Get(), Tag);
}

TArray<FSeinProductionQueueItemInfo> USeinEntityViewModel::GetProductionQueue() const
{
	TArray<FSeinProductionQueueItemInfo> Result;
	if (!WorldSubsystem.IsValid() || !Entity.IsValid()) return Result;

	const FSeinProductionComponent* ProdComp = WorldSubsystem->GetComponent<FSeinProductionComponent>(Entity);
	if (!ProdComp || ProdComp->Queue.Num() == 0) return Result;

	Result.Reserve(ProdComp->Queue.Num());
	for (int32 Idx = 0; Idx < ProdComp->Queue.Num(); ++Idx)
	{
		const FSeinProductionQueueEntry& Entry = ProdComp->Queue[Idx];

		FSeinProductionQueueItemInfo Info;
		Info.QueueIndex = Idx;
		Info.TotalBuildTime = Entry.TotalBuildTime.ToFloat();
		Info.bIsResearch = Entry.bIsResearch;

		// Front entry carries progress + stall state; queue-tail entries are 0/false.
		if (Idx == 0)
		{
			Info.bStalledAtCompletion = ProdComp->bStalledAtCompletion;
			if (Entry.TotalBuildTime > FFixedPoint::Zero)
			{
				const FFixedPoint Ratio = ProdComp->CurrentBuildProgress / Entry.TotalBuildTime;
				const float Clamped = FMath::Clamp(Ratio.ToFloat(), 0.0f, 1.0f);
				Info.ProgressPercent = Clamped;
			}
		}

		// Resolve display name + icon. For unit entries: producible's CDO.
		// For research entries: the granted effect's CDO carries name/EffectTag (no
		// icon convention yet — designers fall back to a research-specific UI).
		if (Entry.bIsResearch)
		{
			if (const USeinEffect* EffectDef = GetDefault<USeinEffect>(Entry.ResearchEffectClass))
			{
				Info.IdentityTag = EffectDef->EffectTag;
				// USeinEffect doesn't carry a designer DisplayName today — use the
				// EffectTag's leaf name as a fallback so the slot renders something
				// readable until effect-side display data lands.
				Info.DisplayName = FText::FromName(EffectDef->EffectTag.GetTagName());
			}
		}
		else if (Entry.ActorClass)
		{
			// Identity now lives on the producible's entity bridge ComponentData.
			TArray<const USeinEntityComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(Entry.ActorClass, Bridges);
			for (const USeinEntityComponent* Bridge : Bridges)
			{
				if (!Bridge) continue;
				if (const FSeinIdentityComponent* Identity = Bridge->FindAuthoredData<FSeinIdentityComponent>())
				{
					Info.DisplayName = Identity->DisplayName;
					Info.Icon = Identity->Icon;
					Info.IdentityTag = Identity->IdentityTag;
					break;
				}
			}
		}

		Result.Add(MoveTemp(Info));
	}

	return Result;
}

FSeinAbilityInfo USeinEntityViewModel::BuildAbilityInfo(const USeinAbility* Ability, FSeinEntityHandle OwnerOverride) const
{
	FSeinAbilityInfo Info;
	if (!Ability)
	{
		return Info;
	}

	Info.Name = Ability->AbilityName;
	Info.AbilityTag = Ability->AbilityTag;
	Info.Icon = Ability->Icon;
	Info.TargetType = Ability->TargetType;
	Info.Cooldown = Ability->Cooldown.ToFloat();
	Info.CooldownRemaining = Ability->CooldownRemaining.ToFloat();
	Info.bIsActive = Ability->bIsActive;
	Info.bIsPassive = Ability->bIsPassive;
	Info.bIsOnCooldown = Ability->IsOnCooldown();

	// Convert tag-keyed resource cost from FFixedPoint to float for display
	for (const auto& Pair : Ability->ResourceCost.Amounts)
	{
		Info.ResourceCost.Add(Pair.Key, Pair.Value.ToFloat());
	}

	// Walk the same activation gates as ProcessCommands::ActivateAbility so the
	// button can read whether a click would actually do anything. Target-validation
	// gates (range / LOS / ValidTargetTags) are intentionally skipped — those are
	// per-click decisions, not per-button. We pass invalid handle + zero vector
	// so SeinAbilityValidation::ValidateTarget short-circuits to Valid.
	//
	// EvalHandle = the entity that actually owns this ability instance, NOT
	// necessarily the selected entity. For squads, the squad-aware aggregation
	// passes the owning member's handle here (see GatherSquadAbilities /
	// FindSquadAbilityByTag — they emit owner handles alongside abilities).
	// Without this routing the availability check would lookup the ability on
	// the squad's component, fail to find it (the ability lives on a member),
	// return Unavailable, and the UI would hide the ability — the bug that
	// caused the squad's deduped ability bar to show nothing on first
	// implementation.
	if (WorldSubsystem.IsValid())
	{
		const FSeinEntityHandle EvalHandle = OwnerOverride.IsValid() ? OwnerOverride : Entity;
		const FSeinAbilityAvailability Availability = USeinAbilityBPFL::SeinGetAbilityAvailability(
			WorldSubsystem.Get(),
			EvalHandle,
			Ability->AbilityTag,
			FSeinEntityHandle(),
			FFixedVector::ZeroVector);

		Info.bIsEnabled     = Availability.bAvailable;
		Info.DisabledReason = Availability.Reason;
	}

	return Info;
}

FSeinAbilityInfo USeinEntityViewModel::MergeAbilityInfos(const TArray<FSeinAbilityInfo>& Inputs)
{
	// Empty input → invalid info. Caller-side guard expected; defensive
	// short-circuit avoids index-0 dereference in the loop below.
	if (Inputs.Num() == 0)
	{
		return FSeinAbilityInfo();
	}

	// Class-level fields cloned from the first input — all inputs for the
	// same tag share these because they're CDO-level (Name, Icon, Cost,
	// Cooldown total, TargetType, bIsPassive don't change per-instance).
	FSeinAbilityInfo Out = Inputs[0];

	// Initialize aggregators from the first input. Runtime state is
	// recomputed below by walking every entry — the first-input values
	// would be overwritten anyway but starting from them keeps the merge
	// loop's "first iteration" branch-free.
	bool bAnyEnabled = Out.bIsEnabled;
	bool bAllOnCooldown = Out.bIsOnCooldown;
	bool bAnyActive = Out.bIsActive;
	float MinCooldownRemaining = Out.CooldownRemaining;
	ESeinAbilityUnavailableReason FirstDisabledReason = Out.DisabledReason;
	bool bAnyHadDisabledReason = !Out.bIsEnabled;

	for (int32 i = 1; i < Inputs.Num(); ++i)
	{
		const FSeinAbilityInfo& In = Inputs[i];
		bAnyEnabled = bAnyEnabled || In.bIsEnabled;
		bAllOnCooldown = bAllOnCooldown && In.bIsOnCooldown;
		bAnyActive = bAnyActive || In.bIsActive;
		// MIN cooldown — "soonest one ready" across owners. Squad-scope
		// cooldowns naturally produce equal values across all squad
		// members holding the same tag (squad cooldown is mirrored to
		// every member's instance), so MIN there is a no-op identity;
		// for selection-level merging across independently-cooldowned
		// units it gives the player-facing "when next firable" value.
		if (In.CooldownRemaining < MinCooldownRemaining)
		{
			MinCooldownRemaining = In.CooldownRemaining;
		}
		// First-disabled reason captures the most-likely-failing gate;
		// only surfaces if no owner is enabled (otherwise DisabledReason
		// gets cleared to None below).
		if (!In.bIsEnabled && !bAnyHadDisabledReason)
		{
			FirstDisabledReason = In.DisabledReason;
			bAnyHadDisabledReason = true;
		}
	}

	Out.bIsEnabled = bAnyEnabled;
	Out.bIsOnCooldown = bAllOnCooldown;
	Out.bIsActive = bAnyActive;
	Out.CooldownRemaining = MinCooldownRemaining;
	Out.DisabledReason = bAnyEnabled ? ESeinAbilityUnavailableReason::None : FirstDisabledReason;

	return Out;
}
