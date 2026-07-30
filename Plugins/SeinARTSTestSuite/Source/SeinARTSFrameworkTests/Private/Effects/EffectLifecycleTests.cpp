#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Containers/Ticker.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Simulation/SeinTestSimContext.h"
#include "Data/SeinWorldSnapshot.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"
#include "UObject/UnrealType.h"

USeinEffectMutationTestHook::FCallback USeinEffectMutationTestHook::Callback;
USeinEffectRemovingPassiveTestAbility::FActivationCallback
	USeinEffectRemovingPassiveTestAbility::ActivationCallback;
USeinEffectRemovingPassiveTestAbility::FEndCallback
	USeinEffectRemovingPassiveTestAbility::EndCallback;

struct FSeinWorldSubsystemTestAccess
{
	enum class EObservedApplyStatus : uint8
	{
		RejectedNoMutation,
		Applied,
		InvalidatedAfterReplacementRemoval
	};

	struct FObservedApplyResult
	{
		EObservedApplyStatus Status = EObservedApplyStatus::RejectedNoMutation;
		int64 EffectInstanceID = 0;
	};

	static FSeinEntityTagState& EntityTags(
		USeinWorldSubsystem& World, FSeinEntityHandle Entity)
	{
		return World.EntityTagStates.FindOrAdd(Entity);
	}

	static void SetNextEffectInstanceID(
		USeinWorldSubsystem& World, int64 NextID)
	{
		World.NextEffectInstanceID = NextID;
	}

	static FObservedApplyResult ApplyEffectDetailed(USeinWorldSubsystem& World,
		FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass,
		FSeinEntityHandle Source)
	{
		const USeinWorldSubsystem::FEffectApplyResult Result =
			World.ApplyEffectTransactional(Target, EffectClass, Source);
		EObservedApplyStatus Status = EObservedApplyStatus::RejectedNoMutation;
		switch (Result.Status)
		{
			case USeinWorldSubsystem::EEffectApplyStatus::Applied:
				Status = EObservedApplyStatus::Applied;
				break;
			case USeinWorldSubsystem::EEffectApplyStatus::
				InvalidatedAfterReplacementRemoval:
				Status = EObservedApplyStatus::
					InvalidatedAfterReplacementRemoval;
				break;
			case USeinWorldSubsystem::EEffectApplyStatus::RejectedNoMutation:
			default:
				break;
		}
		return {Status, Result.EffectInstanceID};
	}
};

namespace
{
	bool MaterializeEffectFixture(
		USeinWorldSubsystem& World,
		TFunctionRef<void()> AuthorState)
	{
		return SeinTestMatchBootstrap::Materialize(
			World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.EffectLifecycle"));
	}

	void ConfigureEffect(USeinEffect& Effect, ESeinModifierScope Scope,
		ESeinEffectDurationMode DurationMode, FFixedPoint TickInterval = FFixedPoint::Zero)
	{
		Effect.Scope = Scope;
		Effect.DurationMode = DurationMode;
		Effect.StackingRule = ESeinEffectStackingRule::Independent;
		Effect.MaxStacks = 64;
		Effect.TickInterval = TickInterval;
	}

	FSeinEntityHandle SpawnEffectEntity(USeinWorldSubsystem& World, FSeinPlayerID Owner)
	{
		check(World.GetMatchBootstrapState() == ESeinMatchBootstrapState::Applying);
		const FSeinEntityHandle Handle = World.SpawnAbstractEntity(FFixedTransform(), Owner);
		World.AddComponent(Handle, FSeinActiveEffectsComponent());
		return Handle;
	}

	bool StartEffectFixture(USeinWorldSubsystem& World)
	{
		if (!SeinTestMatchBootstrap::Start(World))
		{
			return false;
		}
		return World.GetMatchBootstrapState()
			== ESeinMatchBootstrapState::Consumed;
	}

	bool TickForOneSimSecond(USeinWorldSubsystem& World)
	{
		const int32 TickRate = GetDefault<USeinARTSCoreSettings>()->SimulationTickRate;
		if (!World.IsSimulationRunning()
			&& !SeinTestMatchBootstrap::Start(World))
		{
			return false;
		}
		// Fixed-point 1/rate truncates, so one extra tick crosses the exact second.
		for (int32 Tick = 0; Tick <= TickRate; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
		}
		World.StopSimulation();
		return true;
	}

	struct FScopedEffectCallbackReset
	{
		~FScopedEffectCallbackReset()
		{
			USeinEffectMutationTestHook::Callback = nullptr;
			USeinEffectRemovingPassiveTestAbility::ActivationCallback = nullptr;
			USeinEffectRemovingPassiveTestAbility::EndCallback = nullptr;
		}
	};

	struct FScopedEffectTag
	{
		USeinEffect& Effect;
		FGameplayTag Previous;

		FScopedEffectTag(USeinEffect& InEffect, FGameplayTag Tag)
			: Effect(InEffect), Previous(InEffect.EffectTag)
		{
			Effect.EffectTag = Tag;
		}

		~FScopedEffectTag()
		{
			Effect.EffectTag = Previous;
		}
	};

	struct FScopedRemoveEffectsWithTag
	{
		USeinEffect& Effect;
		FGameplayTagContainer Previous;

		FScopedRemoveEffectsWithTag(USeinEffect& InEffect, FGameplayTag Tag)
			: Effect(InEffect), Previous(InEffect.RemoveEffectsWithTag)
		{
			Effect.RemoveEffectsWithTag.Reset();
			Effect.RemoveEffectsWithTag.AddTag(Tag);
		}

		~FScopedRemoveEffectsWithTag()
		{
			Effect.RemoveEffectsWithTag = Previous;
		}
	};

	struct FScopedEffectAbilityConfig
	{
		USeinEffect& Effect;
		TArray<TSubclassOf<USeinAbility>> PreviousAbilities;
		FGameplayTag PreviousTargetTag;

		FScopedEffectAbilityConfig(USeinEffect& InEffect,
			TArray<TSubclassOf<USeinAbility>> InAbilities,
			FGameplayTag InTargetTag = FGameplayTag())
			: Effect(InEffect)
			, PreviousAbilities(InEffect.GrantedAbilities)
			, PreviousTargetTag(InEffect.AbilityTargetClassTag)
		{
			Effect.GrantedAbilities = MoveTemp(InAbilities);
			Effect.AbilityTargetClassTag = InTargetTag;
		}

		~FScopedEffectAbilityConfig()
		{
			Effect.GrantedAbilities = MoveTemp(PreviousAbilities);
			Effect.AbilityTargetClassTag = PreviousTargetTag;
		}
	};

	struct FScopedStackingConfig
	{
		USeinEffect& Effect;
		ESeinEffectStackingRule PreviousRule;
		int32 PreviousMaxStacks;

		FScopedStackingConfig(USeinEffect& InEffect, int32 InMaxStacks)
			: Effect(InEffect)
			, PreviousRule(InEffect.StackingRule)
			, PreviousMaxStacks(InEffect.MaxStacks)
		{
			Effect.StackingRule = ESeinEffectStackingRule::Stack;
			Effect.MaxStacks = InMaxStacks;
		}

		~FScopedStackingConfig()
		{
			Effect.StackingRule = PreviousRule;
			Effect.MaxStacks = PreviousMaxStacks;
		}
	};

	struct FScopedIndependentStackLimit
	{
		USeinEffect& Effect;
		ESeinEffectStackingRule PreviousRule;
		int32 PreviousMaxStacks;

		FScopedIndependentStackLimit(USeinEffect& InEffect, int32 InMaxStacks)
			: Effect(InEffect)
			, PreviousRule(InEffect.StackingRule)
			, PreviousMaxStacks(InEffect.MaxStacks)
		{
			Effect.StackingRule = ESeinEffectStackingRule::Independent;
			Effect.MaxStacks = InMaxStacks;
		}

		~FScopedIndependentStackLimit()
		{
			Effect.StackingRule = PreviousRule;
			Effect.MaxStacks = PreviousMaxStacks;
		}
	};

	struct FScopedFutureEffectBehavior
	{
		USeinEffect& Effect;
		FGameplayTag PreviousDefaultTargetClassTag;
		FFixedPoint PreviousTickInterval;
		bool bPreviousRemoveOnSourceDeath = false;
		TArray<FSeinModifier> PreviousModifiers;

		explicit FScopedFutureEffectBehavior(USeinEffect& InEffect)
			: Effect(InEffect)
			, PreviousDefaultTargetClassTag(InEffect.DefaultTargetClassTag)
			, PreviousTickInterval(InEffect.TickInterval)
			, bPreviousRemoveOnSourceDeath(InEffect.bRemoveOnSourceDeath)
			, PreviousModifiers(InEffect.Modifiers)
		{
			Effect.DefaultTargetClassTag = FGameplayTag();
			Effect.TickInterval = FFixedPoint::Zero;
			Effect.bRemoveOnSourceDeath = false;
			Effect.Modifiers.Reset();
		}

		~FScopedFutureEffectBehavior()
		{
			Effect.DefaultTargetClassTag = PreviousDefaultTargetClassTag;
			Effect.TickInterval = PreviousTickInterval;
			Effect.bRemoveOnSourceDeath = bPreviousRemoveOnSourceDeath;
			Effect.Modifiers = MoveTemp(PreviousModifiers);
		}
	};

	struct FScopedGrantedTags
	{
		USeinEffect& Effect;
		FGameplayTagContainer Previous;

		FScopedGrantedTags(USeinEffect& InEffect, FGameplayTag Tag)
			: Effect(InEffect), Previous(InEffect.GrantedTags)
		{
			Effect.GrantedTags.Reset();
			Effect.GrantedTags.AddTag(Tag);
		}

		~FScopedGrantedTags()
		{
			Effect.GrantedTags = Previous;
		}
	};

	struct FScopedReplayActorConfig
	{
		USeinEntityComponent* Bridge = nullptr;
		TArray<FInstancedStruct> PreviousComponentData;
		FGameplayTagContainer PreviousBaseTags;

		explicit FScopedReplayActorConfig(FGameplayTag ClassTag)
		{
			TArray<const USeinEntityComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(
				ASeinEffectReplayTestActor::StaticClass(), Bridges);
			check(!Bridges.IsEmpty());
			Bridge = const_cast<USeinEntityComponent*>(Bridges[0]);
			PreviousComponentData = Bridge->ComponentData;
			PreviousBaseTags = Bridge->BaseTags;
			Bridge->ComponentData.Add(FInstancedStruct::Make(FSeinAbilityComponent()));
			Bridge->BaseTags.AddTag(ClassTag);
		}

		~FScopedReplayActorConfig()
		{
			Bridge->ComponentData = MoveTemp(PreviousComponentData);
			Bridge->BaseTags = MoveTemp(PreviousBaseTags);
		}
	};
}

USeinEffectRemovingPassiveTestAbility::USeinEffectRemovingPassiveTestAbility()
{
	bIsPassive = true;
}

void USeinEffectRemovingPassiveTestAbility::OnActivate_Implementation()
{
	if (ActivationCallback)
	{
		ActivationCallback(OwnerEntity);
	}
}

void USeinEffectRemovingPassiveTestAbility::OnEnd_Implementation(bool /*bWasCancelled*/)
{
	if (EndCallback)
	{
		EndCallback(OwnerEntity);
	}
}

void USeinEffectMutationTestHook::ProcessEvent(UFunction* Function, void* Parameters)
{
	if (Function && Callback)
	{
		const FName EventName = Function->GetFName();
		if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply)
			|| EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnTick)
			|| EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnExpire)
			|| EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
		{
			FSeinEntityHandle Target;
			if (const FStructProperty* TargetProperty = FindFProperty<FStructProperty>(Function, TEXT("Target")))
			{
				Target = *TargetProperty->ContainerPtrToValuePtr<FSeinEntityHandle>(Parameters);
			}
			Callback(*this, EventName, Target);
		}
	}
	Super::ProcessEvent(Function, Parameters);
}

USeinEffectIdentityInstanceTestEffect::USeinEffectIdentityInstanceTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Instance, ESeinEffectDurationMode::Persistent);
}

USeinEffectIdentityClassTestEffect::USeinEffectIdentityClassTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Class, ESeinEffectDurationMode::Persistent);
}

USeinEffectIdentityPlayerTestEffect::USeinEffectIdentityPlayerTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Player,
		ESeinEffectDurationMode::Persistent, FFixedPoint::One);
}

USeinEffectIdentityInstantTestEffect::USeinEffectIdentityInstantTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Instance, ESeinEffectDurationMode::Instant);
}

USeinEffectPeriodicATestEffect::USeinEffectPeriodicATestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Instance,
		ESeinEffectDurationMode::Persistent, FFixedPoint::One);
}

USeinEffectPeriodicBTestEffect::USeinEffectPeriodicBTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Instance,
		ESeinEffectDurationMode::Persistent, FFixedPoint::One);
}

USeinEffectTimedPlayerTestEffect::USeinEffectTimedPlayerTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Player, ESeinEffectDurationMode::Timed);
	Duration = FFixedPoint::One;
}

USeinEffectPassiveGrantTestEffect::USeinEffectPassiveGrantTestEffect()
{
	ConfigureEffect(*this, ESeinModifierScope::Instance, ESeinEffectDurationMode::Persistent);
	GrantedAbilities.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
	GrantedAbilities.Add(USeinEffectLedgerTestAbility::StaticClass());
}

namespace UE::SeinARTSTests
{
	TEST(EffectIDsAreWorldGlobalAndSurviveSnapshotSerialization, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		int64 InstanceA = 0;
		int64 InstanceB = 0;
		int64 ClassEffect = 0;
		int64 PlayerEffect = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			First = SpawnEffectEntity(*World, Player);
			Second = SpawnEffectEntity(*World, Player);
			InstanceA = World->ApplyEffect(First,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
			InstanceB = World->ApplyEffect(Second,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
			ClassEffect = World->ApplyEffect(First,
				USeinEffectIdentityClassTestEffect::StaticClass(), First);
			PlayerEffect = World->ApplyEffect(First,
				USeinEffectIdentityPlayerTestEffect::StaticClass(), First);
		})));

		ASSERT_THAT(AreEqual(int64{1}, InstanceA));
		ASSERT_THAT(AreEqual(int64{2}, InstanceB));
		ASSERT_THAT(AreEqual(int64{3}, ClassEffect));
		ASSERT_THAT(AreEqual(int64{4}, PlayerEffect));
		ASSERT_THAT(IsTrue(StartEffectFixture(*World)));

		FSeinWorldSnapshot Captured;
		World->CaptureSnapshot(Captured);
		ASSERT_THAT(AreEqual(FSeinWorldSnapshot::CurrentVersion, Captured.SnapshotVersion));
		ASSERT_THAT(AreEqual(int64{5}, Captured.NextEffectInstanceID));

		TArray<uint8> Bytes;
		FMemoryWriter MemoryWriter(Bytes, true);
		FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Writer, &Captured, nullptr);
		ASSERT_THAT(IsFalse(Writer.IsError()));

		FSeinWorldSnapshot Loaded;
		FMemoryReader MemoryReader(Bytes, true);
		FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
		ASSERT_THAT(IsFalse(Reader.IsError()));
		ASSERT_THAT(AreEqual(int64{5}, Loaded.NextEffectInstanceID));

		FSeinWorldSnapshot Legacy = Loaded;
		Legacy.SnapshotVersion = 1;
		Assert.ExpectError(FString::Printf(
			TEXT("RestoreSnapshot: unsupported version 1 (expected %d)."),
			FSeinWorldSnapshot::CurrentVersion));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Legacy)));

		FSeinWorldSnapshot DuplicateActiveID = Loaded;
		FSeinPlayerState* DuplicatePlayerState = DuplicateActiveID.PlayerStates.Find(Player);
		ASSERT_THAT(IsNotNull(DuplicatePlayerState));
		ASSERT_THAT(AreEqual(1, DuplicatePlayerState->ClassEffects.Num()));
		ASSERT_THAT(AreEqual(1, DuplicatePlayerState->PlayerEffects.Num()));
		DuplicatePlayerState->PlayerEffects[0].EffectInstanceID =
			DuplicatePlayerState->ClassEffects[0].EffectInstanceID;
		Assert.ExpectError(TEXT("RestoreSnapshot: authoritative sim state failed structural preflight."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*World, DuplicateActiveID)));

		FSeinWorldSnapshot ReusingActiveID = Loaded;
		ReusingActiveID.NextEffectInstanceID = 4;
		Assert.ExpectError(TEXT("RestoreSnapshot: next effect ID 4 must exceed max active effect ID 4."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*World, ReusingActiveID)));

		World->EnqueueVisualEvent(FSeinVisualEvent());
		FSeinCollisionSpatialHash* MutableHash =
			World->GetCollisionSpatialHashMutable();
		ASSERT_THAT(IsNotNull(MutableHash));
		MutableHash->FinishStaticRebuild();
		ASSERT_THAT(IsTrue(World->HasPendingVisualEvents()));
		ASSERT_THAT(IsFalse(World->GetCollisionSpatialHash().IsStaticDirty()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Loaded)));
		ASSERT_THAT(IsFalse(World->HasPendingVisualEvents()));
		ASSERT_THAT(IsTrue(World->GetCollisionSpatialHash().IsStaticDirty()));

		FSeinWorldSubsystemTestAccess::FObservedApplyResult AfterRestore;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			AfterRestore = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, First,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
		}
		ASSERT_THAT(IsTrue(AfterRestore.Status
			== FSeinWorldSubsystemTestAccess::EObservedApplyStatus::Applied));
		ASSERT_THAT(AreEqual(int64{5}, AfterRestore.EffectInstanceID));
		World->StopSimulation();
	}

	TEST(SnapshotAllocatorRejectsAnActiveInstanceEffectID, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			EffectID = World->ApplyEffect(
				Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(AreEqual(int64{1}, EffectID));
		ASSERT_THAT(IsTrue(StartEffectFixture(*World)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		Snapshot.NextEffectInstanceID = 1;
		const int32 HashBeforeRejectedRestore = World->ComputeStateHash();
		Assert.ExpectError(TEXT("RestoreSnapshot: next effect ID 1 must exceed max active effect ID 1."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Snapshot)));
		ASSERT_THAT(AreEqual(HashBeforeRejectedRestore, World->ComputeStateHash()));
		World->StopSimulation();
	}

	TEST(SnapshotRestoreDropsAppliesFromTheAbandonedTimeline, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		})));
		ASSERT_THAT(IsTrue(StartEffectFixture(*World)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(AreEqual(int64{0}, World->ApplyEffect(
				Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target)));
		}
		ASSERT_THAT(AreEqual(0,
			World->GetComponent<FSeinActiveEffectsComponent>(Target)->ActiveEffects.Num()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Snapshot)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();
		ASSERT_THAT(AreEqual(0,
			World->GetComponent<FSeinActiveEffectsComponent>(Target)->ActiveEffects.Num()));
	}

	TEST(EffectAllocatorProgressIsPartOfTheStateHash, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem* FirstWorld = FirstSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* SecondWorld = SecondSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(FirstWorld));
		ASSERT_THAT(IsNotNull(SecondWorld));

		int32 FirstInitialHash = 0;
		int32 SecondInitialHash = 0;
		int32 FirstAdvancedHash = 0;
		int32 SecondAdvancedHash = 0;
		int64 FirstEffectID = 0;
		int64 SecondEffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*FirstWorld, [&]()
		{
			const FSeinEntityHandle First =
				SpawnEffectEntity(*FirstWorld, FSeinPlayerID::Neutral());
			FirstInitialHash = FirstWorld->ComputeStateHash();
			FirstEffectID = FirstWorld->ApplyEffect(First,
				USeinEffectIdentityInstantTestEffect::StaticClass(), First);
			FirstAdvancedHash = FirstWorld->ComputeStateHash();
		})));
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*SecondWorld, [&]()
		{
			const FSeinEntityHandle Second =
				SpawnEffectEntity(*SecondWorld, FSeinPlayerID::Neutral());
			SecondInitialHash = SecondWorld->ComputeStateHash();
			SecondEffectID = SecondWorld->ApplyEffect(Second,
				USeinEffectIdentityInstantTestEffect::StaticClass(), Second);
			SecondAdvancedHash = SecondWorld->ComputeStateHash();
		})));

		ASSERT_THAT(AreEqual(FirstInitialHash, SecondInitialHash));
		ASSERT_THAT(AreEqual(int64{1}, FirstEffectID));
		ASSERT_THAT(IsFalse(FirstAdvancedHash == SecondInitialHash));
		ASSERT_THAT(AreEqual(int64{1}, SecondEffectID));
		ASSERT_THAT(AreEqual(FirstAdvancedHash, SecondAdvancedHash));
	}

	TEST(EffectCallbacksMayRemoveSiblingEffectsSafely, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 FirstID = 0;
		int64 SecondID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			FirstID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			SecondID = World->ApplyEffect(
				Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(FirstID > 0 && SecondID > FirstID));

		int32 FirstTicks = 0;
		int32 SecondTicks = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle CallbackTarget)
		{
			if (EventName != GET_FUNCTION_NAME_CHECKED(USeinEffect, OnTick)) return;
			if (Effect.IsA<USeinEffectPeriodicATestEffect>())
			{
				++FirstTicks;
				World->RemoveEffect(CallbackTarget, SecondID, false);
			}
			else if (Effect.IsA<USeinEffectPeriodicBTestEffect>())
			{
				++SecondTicks;
			}
		};

		ASSERT_THAT(IsTrue(TickForOneSimSecond(*World)));
		ASSERT_THAT(AreEqual(1, FirstTicks));
		ASSERT_THAT(AreEqual(0, SecondTicks));
		const FSeinActiveEffectsComponent* Effects = World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(1, Effects->ActiveEffects.Num()));
	}

	TEST(EffectTeardownIsReentrantAndUsesOneRemovalPath, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 FirstID = 0;
		int64 SecondID = 0;
		int32 RemovalCallbacks = 0;
		bool bRecursiveSelfRemovalResult = true;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle CallbackTarget)
		{
			if (EventName != GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved)) return;
			++RemovalCallbacks;
			if (Effect.IsA<USeinEffectPeriodicATestEffect>())
			{
				bRecursiveSelfRemovalResult = World->RemoveEffect(CallbackTarget, FirstID, false);
				World->RemoveEffect(CallbackTarget, SecondID, false);
			}
		};

		bool bInitialRemovalSucceeded = false;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			FirstID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			SecondID = World->ApplyEffect(
				Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			bInitialRemovalSucceeded = World->RemoveEffect(Target, FirstID, false);
		})));
		ASSERT_THAT(IsTrue(bInitialRemovalSucceeded));
		ASSERT_THAT(IsFalse(bRecursiveSelfRemovalResult));
		ASSERT_THAT(AreEqual(2, RemovalCallbacks));
		const FSeinActiveEffectsComponent* Effects = World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(0, Effects->ActiveEffects.Num()));
	}

	TEST(EffectLifecycleEventsStayCausalWhenOnApplyRemovesItself, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 RemovedID = 0;
		bool bSawActiveInstance = false;
		bool bRemovalSucceeded = false;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle CallbackTarget)
		{
			if (EventName != GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply)
				|| !Effect.IsA<USeinEffectPeriodicATestEffect>())
			{
				return;
			}
			const FSeinActiveEffectsComponent* Active =
				World->GetComponent<FSeinActiveEffectsComponent>(CallbackTarget);
			if (!Active || Active->ActiveEffects.Num() != 1) return;
			bSawActiveInstance = true;
			RemovedID = Active->ActiveEffects[0].EffectInstanceID;
			bRemovalSucceeded = World->RemoveEffectByID(RemovedID, /*bByExpiration=*/false);
		};

		int64 AppliedID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->FlushVisualEvents();
			AppliedID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(bSawActiveInstance));
		ASSERT_THAT(IsTrue(bRemovalSucceeded));
		ASSERT_THAT(AreEqual(AppliedID, RemovedID));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsFalse(World->RemoveEffectByID(
				AppliedID, /*bByExpiration=*/false)));
		}

		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectApplied),
			static_cast<uint8>(Events[0].Type)));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[1].Type)));
		World->StopSimulation();
	}

	TEST(InstantEffectDestroyedTargetStillCompletesRemovalLifecycle, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int32 ApplyCalls = 0;
		int32 RemovedCalls = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle CallbackTarget)
		{
			if (!Effect.IsA<USeinEffectIdentityInstantTestEffect>()) return;
			if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply))
			{
				++ApplyCalls;
				World->DestroyEntity(CallbackTarget);
			}
			else if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
			{
				++RemovedCalls;
			}
		};

		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->FlushVisualEvents();
		})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			EffectID = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target,
				USeinEffectIdentityInstantTestEffect::StaticClass(), Target)
				.EffectInstanceID;
		}
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(1, ApplyCalls));
		ASSERT_THAT(AreEqual(1, RemovedCalls));
		ASSERT_THAT(IsFalse(World->IsEntityAlive(Target)));
		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNull(Effects));

		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectApplied),
			static_cast<uint8>(Events[0].Type)));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[1].Type)));
		const int32 TombstoneGeneration =
			World->GetEntityPool().GetSlotGeneration(Target.Index);
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(TombstoneGeneration
			== World->GetEntityPool().GetSlotGeneration(Target.Index)));
		World->StopSimulation();
	}

	TEST(PlayerEffectQueriesAreScopeExplicitAndIDsRemoveGlobally, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag QueryTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag InstanceTag(*GetMutableDefault<USeinEffectIdentityInstanceTestEffect>(), QueryTag);
		FScopedEffectTag ClassTag(*GetMutableDefault<USeinEffectIdentityClassTestEffect>(), QueryTag);
		FScopedEffectTag PlayerTag(*GetMutableDefault<USeinEffectIdentityPlayerTestEffect>(), QueryTag);

		const FSeinPlayerID Player(9);
		FSeinEntityHandle Target;
		int64 InstanceID = 0;
		int64 ClassID = 0;
		int64 PlayerID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Target = SpawnEffectEntity(*World, Player);
			InstanceID = World->ApplyEffect(
				Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
			ClassID = World->ApplyEffect(
				Target, USeinEffectIdentityClassTestEffect::StaticClass(), Target);
			PlayerID = World->ApplyEffect(
				Target, USeinEffectIdentityPlayerTestEffect::StaticClass(), Target);
		})));

		ASSERT_THAT(IsTrue(World->HasInstanceEffectWithTag(Target, QueryTag)));
		ASSERT_THAT(AreEqual(1, World->GetInstanceEffectStacks(Target, QueryTag)));
		ASSERT_THAT(IsTrue(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Class, QueryTag)));
		ASSERT_THAT(IsTrue(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Player, QueryTag)));
		ASSERT_THAT(AreEqual(1, World->GetEffectStacksForPlayer(Player, ESeinModifierScope::Class, QueryTag)));
		ASSERT_THAT(AreEqual(1, World->GetEffectStacksForPlayer(Player, ESeinModifierScope::Player, QueryTag)));

		// Instance is intentionally invalid for a player-only query: there is no
		// entity target from which to select Instance storage.
		ASSERT_THAT(IsFalse(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Instance, QueryTag)));
		ASSERT_THAT(AreEqual(0, World->GetEffectStacksForPlayer(Player, ESeinModifierScope::Instance, QueryTag)));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		bool bRemovedClass = false;
		bool bPlayerStillPresentAfterClassRemoval = false;
		bool bRemovedPlayer = false;
		bool bRemovedInstance = false;
		bool bRemovedInstanceTwice = true;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			bRemovedClass = World->RemoveEffectByID(ClassID, false);
			bPlayerStillPresentAfterClassRemoval =
				World->HasEffectWithTagForPlayer(
					Player, ESeinModifierScope::Player, QueryTag);
			bRemovedPlayer = World->RemoveEffectByID(PlayerID, false);
			bRemovedInstance = World->RemoveEffectByID(InstanceID, false);
			bRemovedInstanceTwice = World->RemoveEffectByID(InstanceID, false);
		}
		ASSERT_THAT(IsTrue(bRemovedClass));
		ASSERT_THAT(IsFalse(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Class, QueryTag)));
		ASSERT_THAT(IsTrue(bPlayerStillPresentAfterClassRemoval));
		ASSERT_THAT(IsTrue(bRemovedPlayer));
		ASSERT_THAT(IsTrue(bRemovedInstance));
		ASSERT_THAT(IsFalse(World->HasInstanceEffectWithTag(Target, QueryTag)));
		ASSERT_THAT(IsFalse(bRemovedInstanceTwice));
		World->StopSimulation();
	}

	TEST(PassiveGrantRemovalSkipsOnApplyAndPublishesNoDeadAbilityID, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int32 PassiveActivations = 0;
		int32 EffectApplyCallbacks = 0;
		int32 EffectRemovalCallbacks = 0;
		bool bPassiveRemovalSucceeded = false;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner)
		{
			++PassiveActivations;
			const FSeinActiveEffectsComponent* Active =
				World->GetComponent<FSeinActiveEffectsComponent>(Owner);
			if (!Active || Active->ActiveEffects.Num() != 1) return;
			bPassiveRemovalSucceeded = World->RemoveEffectByID(
				Active->ActiveEffects[0].EffectInstanceID, /*bByExpiration=*/false);
		};
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle)
		{
			if (!Effect.IsA<USeinEffectPassiveGrantTestEffect>()) return;
			if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply)) ++EffectApplyCallbacks;
			if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved)) ++EffectRemovalCallbacks;
		};

		int64 AssignedID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
			World->FlushVisualEvents();
			AssignedID = World->ApplyEffect(
				Target, USeinEffectPassiveGrantTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(AssignedID > 0));
		ASSERT_THAT(AreEqual(1, PassiveActivations));
		ASSERT_THAT(IsTrue(bPassiveRemovalSucceeded));
		ASSERT_THAT(AreEqual(0, EffectApplyCallbacks));
		ASSERT_THAT(AreEqual(1, EffectRemovalCallbacks));

		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		const FSeinAbilityComponent* Abilities = World->GetComponent<FSeinAbilityComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(IsNotNull(Abilities));
		ASSERT_THAT(AreEqual(0, Effects->ActiveEffects.Num()));
		ASSERT_THAT(AreEqual(0, Abilities->GrantedAbilities.Num()));
		ASSERT_THAT(AreEqual(0, Abilities->AbilityInstanceIDs.Num()));
		ASSERT_THAT(AreEqual(0, Abilities->AbilityGrantCounts.Num()));
		ASSERT_THAT(AreEqual(0, Abilities->ActivePassiveIDs.Num()));

		TArray<ESeinVisualEventType> EffectEventTypes;
		for (const FSeinVisualEvent& Event : World->FlushVisualEvents())
		{
			if (Event.Type == ESeinVisualEventType::EffectApplied
				|| Event.Type == ESeinVisualEventType::EffectRemoved)
			{
				EffectEventTypes.Add(Event.Type);
			}
		}
		ASSERT_THAT(AreEqual(2, EffectEventTypes.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectApplied),
			static_cast<uint8>(EffectEventTypes[0])));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(EffectEventTypes[1])));
	}

	TEST(PartialPassiveSelfRemovalPreservesAnUnreachedPreexistingGrant, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int32 PreexistingAbilityID = INDEX_NONE;
		bool bRemovedDuringFirstGrant = false;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner)
		{
			const FSeinActiveEffectsComponent* Active =
				World->GetComponent<FSeinActiveEffectsComponent>(Owner);
			if (Active && Active->ActiveEffects.Num() == 1)
			{
				bRemovedDuringFirstGrant = World->RemoveEffectByID(
					Active->ActiveEffects[0].EffectInstanceID, false);
			}
		};

		int64 AppliedEffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
			PreexistingAbilityID = USeinAbilityBPFL::SeinGrantAbility(
				World, Target, USeinEffectLedgerTestAbility::StaticClass());
			AppliedEffectID = World->ApplyEffect(Target,
				USeinEffectPassiveGrantTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(PreexistingAbilityID != INDEX_NONE));
		ASSERT_THAT(IsTrue(AppliedEffectID > 0));
		ASSERT_THAT(IsTrue(bRemovedDuringFirstGrant));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectRemovingPassiveTestAbility::StaticClass())));
	}

	TEST(CommittedGrantLedgerSurvivesSnapshotRoundTripAndRevokes, "SeinARTS.Unit.Effects")
	{
		TArray<TSubclassOf<USeinAbility>> GrantClasses;
		GrantClasses.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityInstanceTestEffect>(), MoveTemp(GrantClasses));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
			EffectID = World->ApplyEffect(Target,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(IsTrue(StartEffectFixture(*World)));

		FSeinWorldSnapshot Captured;
		World->CaptureSnapshot(Captured);
		TArray<uint8> Bytes;
		{
			FMemoryWriter MemoryWriter(Bytes, true);
			FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
			FSeinWorldSnapshot::StaticStruct()->SerializeItem(Writer, &Captured, nullptr);
			ASSERT_THAT(IsFalse(Writer.IsError()));
		}
		FSeinWorldSnapshot Loaded;
		{
			FMemoryReader MemoryReader(Bytes, true);
			FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
			FSeinWorldSnapshot::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
			ASSERT_THAT(IsFalse(Reader.IsError()));
		}

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		}
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Loaded)));
		const FSeinActiveEffectsComponent* RestoredEffects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(RestoredEffects));
		ASSERT_THAT(AreEqual(1, RestoredEffects->ActiveEffects.Num()));
		ASSERT_THAT(AreEqual(1,
			RestoredEffects->ActiveEffects[0].CommittedAbilityGrants.Num()));
		ASSERT_THAT(IsTrue(Target
			== RestoredEffects->ActiveEffects[0].CommittedAbilityGrants[0].Recipient));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		}
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
		World->StopSimulation();
	}

	TEST(ReplayReResolvesEffectsAfterAPassiveRemovesAnEarlierEntry, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);

		TArray<TSubclassOf<USeinAbility>> FirstGrant;
		FirstGrant.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
		FScopedEffectAbilityConfig FirstConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(FirstGrant), ClassTag);
		TArray<TSubclassOf<USeinAbility>> LaterGrant;
		LaterGrant.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig LaterConfig(
			*GetMutableDefault<USeinEffectIdentityPlayerTestEffect>(),
			MoveTemp(LaterGrant), ClassTag);

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(12);
		FSeinEntityHandle Driver;
		FSeinEntityHandle Recipient;
		int64 FirstID = 0;
		int64 LaterID = 0;
		bool bRemovedFirst = false;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle)
		{
			bRemovedFirst = World->RemoveEffectByID(FirstID, false);
		};
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Driver = SpawnEffectEntity(*World, Player);
			FirstID = World->ApplyEffect(Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
			LaterID = World->ApplyEffect(Driver,
				USeinEffectIdentityPlayerTestEffect::StaticClass(), Driver);
			Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
		})));
		ASSERT_THAT(IsTrue(FirstID > 0 && LaterID > FirstID));
		ASSERT_THAT(IsTrue(Recipient.IsValid()));
		ASSERT_THAT(IsTrue(bRemovedFirst));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));

		const FSeinPlayerState* State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(0, State->ClassEffects.Num()));
		ASSERT_THAT(AreEqual(1, State->PlayerEffects.Num()));
		ASSERT_THAT(AreEqual(LaterID, State->PlayerEffects[0].EffectInstanceID));
		ASSERT_THAT(AreEqual(1, State->PlayerEffects[0].CommittedAbilityGrants.Num()));
		ASSERT_THAT(IsTrue(Recipient
			== State->PlayerEffects[0].CommittedAbilityGrants[0].Recipient));
	}

	TEST(StackReapplySaturatesAtInt32MaxWithoutOverflow, "SeinARTS.Unit.Effects")
	{
		FScopedStackingConfig Stacking(
			*GetMutableDefault<USeinEffectIdentityInstanceTestEffect>(), MAX_int32);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 EffectID = 0;
		int64 ReappliedID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			EffectID = World->ApplyEffect(Target,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
			FSeinActiveEffectsComponent* Effects =
				World->GetComponent<FSeinActiveEffectsComponent>(Target);
			check(Effects && Effects->ActiveEffects.Num() == 1);
			Effects->ActiveEffects[0].CurrentStacks = MAX_int32;
			ReappliedID = World->ApplyEffect(Target,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		})));
		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(EffectID, ReappliedID));
		ASSERT_THAT(AreEqual(MAX_int32, Effects->ActiveEffects[0].CurrentStacks));
	}

	TEST(EffectGrantLedgerContentParticipatesInStateHash, "SeinARTS.Unit.Effects")
	{
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityInstanceTestEffect>(), MoveTemp(Grants));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		int32 Before = 0;
		int32 After = 0;
		int32 GrantCount = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			const FSeinEntityHandle Target =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			const FSeinEntityHandle Alternate =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
			World->ApplyEffect(Target,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
			FSeinActiveEffectsComponent* Effects =
				World->GetComponent<FSeinActiveEffectsComponent>(Target);
			check(Effects);
			GrantCount = Effects->ActiveEffects[0].CommittedAbilityGrants.Num();
			Before = World->ComputeStateHash();
			const FSeinEntityHandle OriginalRecipient =
				Effects->ActiveEffects[0].CommittedAbilityGrants[0].Recipient;
			Effects->ActiveEffects[0].CommittedAbilityGrants[0].Recipient = Alternate;
			After = World->ComputeStateHash();
			Effects->ActiveEffects[0].CommittedAbilityGrants[0].Recipient =
				OriginalRecipient;
		})));
		ASSERT_THAT(AreEqual(1, GrantCount));
		ASSERT_THAT(IsFalse(Before == After));
	}

	TEST(ForceRevokeThenRegrantCannotBeStolenByOldEffectRemoval, "SeinARTS.Unit.Effects")
	{
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityInstanceTestEffect>(), MoveTemp(Grants));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 EffectID = 0;
		int32 RevokedCount = 0;
		int32 RegrantID = INDEX_NONE;
		bool bRemoved = false;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
			EffectID = World->ApplyEffect(Target,
				USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
			RevokedCount = USeinAbilityBPFL::SeinForceRevokeAbilityByClass(
				World, Target, USeinEffectLedgerTestAbility::StaticClass());
			RegrantID = USeinAbilityBPFL::SeinGrantAbility(
				World, Target, USeinEffectLedgerTestAbility::StaticClass());
			bRemoved = World->RemoveEffectByID(EffectID, false);
		})));
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(1, RevokedCount));
		ASSERT_THAT(IsTrue(RegrantID != INDEX_NONE));
		ASSERT_THAT(IsTrue(bRemoved));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
	}

	TEST(OwnerTransferSwapsOnlyEffectOwnedAbilityReferences, "SeinARTS.Unit.Effects")
	{
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID PlayerA(31);
		const FSeinPlayerID PlayerB(32);
		FSeinEntityHandle Recipient;
		int64 EffectA = 0;
		int64 EffectB = 0;
		int32 ExplicitGrantID = INDEX_NONE;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(PlayerA, FSeinFactionID(1));
			World->RegisterPlayer(PlayerB, FSeinFactionID(1));
			const FSeinEntityHandle DriverA = SpawnEffectEntity(*World, PlayerA);
			const FSeinEntityHandle DriverB = SpawnEffectEntity(*World, PlayerB);
			EffectA = World->ApplyEffect(DriverA,
				USeinEffectIdentityClassTestEffect::StaticClass(), DriverA);
			EffectB = World->ApplyEffect(DriverB,
				USeinEffectIdentityClassTestEffect::StaticClass(), DriverB);
			Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerA);
			ExplicitGrantID = USeinAbilityBPFL::SeinGrantAbility(
				World, Recipient, USeinEffectLedgerTestAbility::StaticClass());
		})));
		ASSERT_THAT(IsTrue(EffectA > 0 && EffectB > EffectA));
		ASSERT_THAT(IsTrue(ExplicitGrantID != INDEX_NONE));
		ASSERT_THAT(AreEqual(2, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetEntityOwner(Recipient, PlayerB);
		}
		ASSERT_THAT(IsTrue(World->GetEntityOwner(Recipient) == PlayerB));
		ASSERT_THAT(AreEqual(2, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));
		const FSeinPlayerState* StateA = World->GetPlayerState(PlayerA);
		const FSeinPlayerState* StateB = World->GetPlayerState(PlayerB);
		ASSERT_THAT(IsNotNull(StateA));
		ASSERT_THAT(IsNotNull(StateB));
		ASSERT_THAT(AreEqual(0, StateA->ClassEffects[0].CommittedAbilityGrants.Num()));
		ASSERT_THAT(AreEqual(1, StateB->ClassEffects[0].CommittedAbilityGrants.Num()));
		ASSERT_THAT(IsTrue(StateB->ClassEffects[0].CommittedAbilityGrants[0].Recipient
			== Recipient));
		World->StopSimulation();
	}

	TEST(OwnerTransferCannotReentrantlySealBootstrapFromAbilityEnd,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID PlayerA(37);
		const FSeinPlayerID PlayerB(38);
		bool bEndCalled = false;
		bool bReentrantSealSucceeded = true;
		FString ReentrantSealError;
		FSeinMatchBootstrapReceipt ReentrantReceipt;
		USeinEffectRemovingPassiveTestAbility::EndCallback =
			[&](FSeinEntityHandle)
			{
				bEndCalled = true;
				bReentrantSealSucceeded = World->SealLocalMatchBootstrap(
					FGuid(0x11000000, 0x22000000, 0x33000000, 0x44000000),
					ReentrantReceipt,
					ReentrantSealError);
			};

		FSeinEntityHandle Recipient;
		const bool bMaterialized = MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(PlayerA, FSeinFactionID(1));
			World->RegisterPlayer(PlayerB, FSeinFactionID(1));
			const FSeinEntityHandle Driver =
				SpawnEffectEntity(*World, PlayerA);
			World->ApplyEffect(
				Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(),
				Driver);
			Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(),
				FFixedTransform(),
				PlayerA);
			World->SetEntityOwner(Recipient, PlayerB);
		});

		ASSERT_THAT(IsTrue(bMaterialized));
		ASSERT_THAT(IsTrue(bEndCalled));
		ASSERT_THAT(IsFalse(bReentrantSealSucceeded));
		ASSERT_THAT(IsFalse(ReentrantReceipt.IsValid()));
		ASSERT_THAT(IsTrue(ReentrantSealError.Contains(
			TEXT("ownership transition"))));
		ASSERT_THAT(IsTrue(World->GetEntityOwner(Recipient) == PlayerB));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(World->GetMatchBootstrapState())));
	}

	TEST(NestedOwnerTransferReplayKeepsReplacementLedgerClaim, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID PlayerA(41);
		const FSeinPlayerID PlayerB(42);
		const FSeinPlayerID PlayerC(43);
		FSeinEntityHandle Recipient;
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(PlayerA, FSeinFactionID(1));
			World->RegisterPlayer(PlayerB, FSeinFactionID(1));
			World->RegisterPlayer(PlayerC, FSeinFactionID(1));
			const FSeinEntityHandle Driver = SpawnEffectEntity(*World, PlayerB);
			EffectID = World->ApplyEffect(Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
			Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerA);
		})));

		int32 Activations = 0;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner)
		{
			++Activations;
			if (Activations == 1)
			{
				World->SetEntityOwner(Owner, PlayerC);
				World->SetEntityOwner(Owner, PlayerB);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetEntityOwner(Recipient, PlayerB);
		}

		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(2, Activations));
		ASSERT_THAT(IsTrue(World->GetEntityOwner(Recipient) == PlayerB));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		const FSeinPlayerState* State = World->GetPlayerState(PlayerB);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(1, State->ClassEffects[0].CommittedAbilityGrants.Num()));
		ASSERT_THAT(IsTrue(State->ClassEffects[0].CommittedAbilityGrants[0].Recipient
			== Recipient));
		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Snapshot)));
		World->StopSimulation();
	}

	TEST(NestedAwayBackTransferBalancesEveryDetachedAbilityClass, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID PlayerA(44);
		const FSeinPlayerID PlayerB(45);
		FSeinEntityHandle Recipient;
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(PlayerA, FSeinFactionID(1));
			World->RegisterPlayer(PlayerB, FSeinFactionID(1));
			const FSeinEntityHandle Driver = SpawnEffectEntity(*World, PlayerB);
			EffectID = World->ApplyEffect(Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
			Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerB);
		})));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));

		int32 EndCallbacks = 0;
		USeinEffectRemovingPassiveTestAbility::EndCallback =
			[&](FSeinEntityHandle Owner)
		{
			++EndCallbacks;
			if (EndCallbacks == 1)
			{
				World->SetEntityOwner(Owner, PlayerB);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetEntityOwner(Recipient, PlayerA);
		}

		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(1, EndCallbacks));
		ASSERT_THAT(IsTrue(World->GetEntityOwner(Recipient) == PlayerB));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));
		const FSeinPlayerState* State = World->GetPlayerState(PlayerB);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(2, State->ClassEffects[0].CommittedAbilityGrants.Num()));
		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Snapshot)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		}
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));
		World->StopSimulation();
	}

	TEST(FanoutRechecksRecipientTagAfterPassiveCallback, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectRemovingPassiveTestAbility::StaticClass());
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(51);
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner)
		{
			if (Owner == First)
			{
				World->UngrantTag(Owner, ClassTag);
			}
		};
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
			First = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
			Second = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
			EffectID = World->ApplyEffect(Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		})));
		ASSERT_THAT(IsTrue(EffectID > 0));

		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, First, USeinEffectLedgerTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Second, USeinEffectLedgerTestAbility::StaticClass())));
		const FSeinPlayerState* State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(3, State->ClassEffects[0].CommittedAbilityGrants.Num()));
	}

	TEST(PassiveDestroySkipsLaterGrantsAndOnApply, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int32 ApplyCallbacks = 0;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner) { World->DestroyEntity(Owner); };
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle)
		{
			if (Effect.IsA<USeinEffectPassiveGrantTestEffect>()
				&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply))
			{
				++ApplyCallbacks;
			}
		};
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->AddComponent(Target, FSeinAbilityComponent());
		})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			EffectID = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target,
				USeinEffectPassiveGrantTestEffect::StaticClass(), Target)
				.EffectInstanceID;
		}
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(0, ApplyCallbacks));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();
	}

	TEST(DestroyObserverIsReadOnlyAndCanInspectTheExactTombstone,
		"SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Owner(62);
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Owner, FSeinFactionID(1));
			First = SpawnEffectEntity(*World, Owner);
			Second = SpawnEffectEntity(*World, Owner);
		})));
		bool bSawDestroyingComponent = false;
		bool bSawDestroyingEntity = false;
		FSeinPlayerID DestroyingOwner = FSeinPlayerID::Neutral();
		const FDelegateHandle Callback = World->OnEntityDestroyed.AddLambda(
			[&](FSeinEntityHandle Destroyed)
			{
				if (Destroyed != First) return;
				bSawDestroyingComponent =
					World->GetDestroyingComponent<FSeinActiveEffectsComponent>(
						Destroyed) != nullptr;
				bSawDestroyingEntity =
					World->GetDestroyingEntity(Destroyed) != nullptr;
				DestroyingOwner = World->GetDestroyingEntityOwner(Destroyed);
				World->DestroyEntity(Second);
			});
		Assert.ExpectError(TEXT("DestroyEntity rejected outside bootstrap Applying"));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(First);
		}
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsNull(World->GetEntityPool().Get(First)));
		ASSERT_THAT(IsTrue(bSawDestroyingComponent));
		ASSERT_THAT(IsTrue(bSawDestroyingEntity));
		ASSERT_THAT(IsTrue(DestroyingOwner == Owner));
		ASSERT_THAT(IsNotNull(World->GetEntityPool().Get(Second)));
		ASSERT_THAT(IsTrue(World->GetEntityPool().Get(Second)->IsAlive()));
		ASSERT_THAT(IsNull(
			World->GetDestroyingComponent<FSeinActiveEffectsComponent>(First)));
		ASSERT_THAT(IsNull(World->GetDestroyingEntity(First)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(Second);
		}
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();
		ASSERT_THAT(IsNull(World->GetEntityPool().Get(Second)));
		World->OnEntityDestroyed.Remove(Callback);
	}

	TEST(DestroyChurnPrunesPersistentEffectRecipientLedgers, "SeinARTS.Unit.Effects")
	{
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(61);
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
			EffectID = World->ApplyEffect(Driver,
				USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		})));
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		for (int32 Iteration = 0; Iteration < 3; ++Iteration)
		{
			FSeinEntityHandle Recipient;
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				Recipient = World->SpawnEntity(
					ASeinEffectReplayTestActor::StaticClass(),
					FFixedTransform(), Player);
			}
			ASSERT_THAT(AreEqual(1,
				World->GetPlayerState(Player)->ClassEffects[0].CommittedAbilityGrants.Num()));
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				World->DestroyEntity(Recipient);
			}
			FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(AreEqual(0,
				World->GetPlayerState(Player)->ClassEffects[0].CommittedAbilityGrants.Num()));
		}
		World->StopSimulation();
	}

	TEST(SnapshotRejectsMalformedStackScopeAndGrantMultiset, "SeinARTS.Unit.Effects")
	{
		const FGameplayTag ClassTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedReplayActorConfig ActorConfig(ClassTag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinEffectLedgerTestAbility::StaticClass());
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(),
			MoveTemp(Grants), ClassTag);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Player(71);
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
			World->ApplyEffect(
				Driver, USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
			World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
		})));
		ASSERT_THAT(IsTrue(StartEffectFixture(*World)));
		FSeinWorldSnapshot Valid;
		World->CaptureSnapshot(Valid);
		const int32 HashBefore = World->ComputeStateHash();

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT("RestoreSnapshot: authoritative sim state failed structural preflight."));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(*World, Bad)));
			ASSERT_THAT(AreEqual(HashBefore, World->ComputeStateHash()));
		};
		FSeinWorldSnapshot BadStack = Valid;
		BadStack.PlayerStates.FindChecked(Player).ClassEffects[0].CurrentStacks = 2;
		ExpectRejectedWithoutMutation(BadStack);
		FSeinWorldSnapshot BadScope = Valid;
		FSeinPlayerState& BadScopeState = BadScope.PlayerStates.FindChecked(Player);
		BadScopeState.PlayerEffects.Add(BadScopeState.ClassEffects[0]);
		BadScopeState.ClassEffects.Reset();
		ExpectRejectedWithoutMutation(BadScope);
		FSeinWorldSnapshot BadLedger = Valid;
		BadLedger.PlayerStates.FindChecked(Player).ClassEffects[0]
			.CommittedAbilityGrants.Reset();
		ExpectRejectedWithoutMutation(BadLedger);
		World->StopSimulation();
	}

	TEST(ReplacementPreflightRejectsFullIndependentStackWithoutRemovingVictim,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag RemovedTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), RemovedTag);
		FScopedIndependentStackLimit StackLimit(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), 1);
		FSeinEntityHandle Target;
		int64 VictimID = 0;
		int64 ExistingReplacementID = 0;
		int32 HashBefore = 0;
		int32 HashAfter = 0;
		int32 NewEventCount = 0;
		bool bVictimSurvived = false;
		int32 RemovalCallbacks = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle)
			{
				if (Effect.IsA<USeinEffectPeriodicATestEffect>()
					&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
				{
					++RemovalCallbacks;
				}
			};
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			VictimID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			ExistingReplacementID = World->ApplyEffect(
				Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
			HashBefore = World->ComputeStateHash();
			FScopedRemoveEffectsWithTag ReplacementRule(
				*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), RemovedTag);
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			HashAfter = World->ComputeStateHash();
			NewEventCount = World->FlushVisualEvents().Num();
			const FSeinActiveEffectsComponent* Effects =
				World->GetComponent<FSeinActiveEffectsComponent>(Target);
			bVictimSurvived = Effects && Effects->ActiveEffects.ContainsByPredicate(
				[VictimID](const FSeinActiveEffect& Effect)
				{
					return Effect.EffectInstanceID == VictimID;
				});
		})));
		ASSERT_THAT(IsTrue(VictimID > 0 && ExistingReplacementID > VictimID));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::RejectedNoMutation));
		ASSERT_THAT(AreEqual(int64{0}, Result.EffectInstanceID));
		ASSERT_THAT(AreEqual(0, RemovalCallbacks));
		ASSERT_THAT(AreEqual(HashBefore, HashAfter));
		ASSERT_THAT(AreEqual(0, NewEventCount));
		ASSERT_THAT(IsTrue(bVictimSurvived));
	}

	TEST(ReplacementPreflightRejectsTagOverflowWithoutRemovingVictim,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), Tag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		FScopedGrantedTags GrantedTags(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		int64 VictimID = 0;
		int32 HashBefore = 0;
		int32 HashAfter = 0;
		int32 NewEventCount = 0;
		bool bVictimRemoved = false;
		int32 RemovalCallbacks = 0;
		int32 ReplacementRemovalCallbacks = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle)
			{
				if (Effect.IsA<USeinEffectPeriodicATestEffect>()
					&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
				{
					++RemovalCallbacks;
				}
			};
		Assert.ExpectError(TEXT("tag refcount cannot accept"));
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			const FSeinEntityHandle Target =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			VictimID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			FSeinEntityTagState& TagState =
				FSeinWorldSubsystemTestAccess::EntityTags(*World, Target);
			TagState.TagRefCounts.Add(Tag, MAX_int32);
			TagState.CombinedTags.AddTag(Tag);
			World->FlushVisualEvents();
			HashBefore = World->ComputeStateHash();
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			HashAfter = World->ComputeStateHash();
			NewEventCount = World->FlushVisualEvents().Num();
			ReplacementRemovalCallbacks = RemovalCallbacks;
			bVictimRemoved = World->RemoveEffectByID(VictimID, false);
			TagState.TagRefCounts.Reset();
			TagState.CombinedTags.Reset();
		})));
		ASSERT_THAT(IsTrue(VictimID > 0));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::RejectedNoMutation));
		ASSERT_THAT(AreEqual(0, ReplacementRemovalCallbacks));
		ASSERT_THAT(AreEqual(HashBefore, HashAfter));
		ASSERT_THAT(AreEqual(0, NewEventCount));
		ASSERT_THAT(IsTrue(bVictimRemoved));
	}

	TEST(ReplacementPreflightRejectsMalformedAbilityGrantWithoutRemovingVictim,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), Tag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		TArray<TSubclassOf<USeinAbility>> Grants;
		Grants.Add(USeinAbility::StaticClass());
		ASSERT_THAT(IsTrue(USeinAbility::StaticClass()->HasAnyClassFlags(CLASS_Abstract)));
		FScopedEffectAbilityConfig GrantConfig(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), MoveTemp(Grants));
		Assert.ExpectError(TEXT("contains an unusable GrantedAbilities entry"));
		int64 VictimID = 0;
		int32 HashBefore = 0;
		int32 HashAfter = 0;
		int32 NewEventCount = 0;
		bool bVictimRemoved = false;
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			const FSeinEntityHandle Target =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			VictimID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
			HashBefore = World->ComputeStateHash();
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			HashAfter = World->ComputeStateHash();
			NewEventCount = World->FlushVisualEvents().Num();
			bVictimRemoved = World->RemoveEffectByID(VictimID, false);
		})));
		ASSERT_THAT(IsTrue(VictimID > 0));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::RejectedNoMutation));
		ASSERT_THAT(AreEqual(HashBefore, HashAfter));
		ASSERT_THAT(AreEqual(0, NewEventCount));
		ASSERT_THAT(IsTrue(bVictimRemoved));
	}

	TEST(ReplacementPreflightRejectsMissingScopeStorageWithoutRemovingPlayerEffect,
		"SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(72);
		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectIdentityClassTestEffect>(), Tag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		Assert.ExpectError(TEXT("has no storage for scope"));
		int64 VictimID = 0;
		int32 HashBefore = 0;
		int32 HashAfter = 0;
		int32 NewEventCount = 0;
		bool bVictimRemoved = false;
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const FSeinEntityHandle Target = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			VictimID = World->ApplyEffect(Target,
				USeinEffectIdentityClassTestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
			HashBefore = World->ComputeStateHash();
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			HashAfter = World->ComputeStateHash();
			NewEventCount = World->FlushVisualEvents().Num();
			bVictimRemoved = World->RemoveEffectByID(VictimID, false);
		})));
		ASSERT_THAT(IsTrue(VictimID > 0));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::RejectedNoMutation));
		ASSERT_THAT(AreEqual(HashBefore, HashAfter));
		ASSERT_THAT(AreEqual(0, NewEventCount));
		ASSERT_THAT(IsTrue(bVictimRemoved));
	}

	TEST(ReplacementPreflightRejectsExhaustedEffectIDWithoutRemovingVictim,
		"SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), Tag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		Assert.ExpectError(TEXT("Effect ID space exhausted"));
		int64 VictimID = 0;
		int32 HashBefore = 0;
		int32 HashAfter = 0;
		int32 NewEventCount = 0;
		bool bVictimRemoved = false;
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			const FSeinEntityHandle Target =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			VictimID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			FSeinWorldSubsystemTestAccess::SetNextEffectInstanceID(*World, MAX_int64);
			World->FlushVisualEvents();
			HashBefore = World->ComputeStateHash();
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			HashAfter = World->ComputeStateHash();
			NewEventCount = World->FlushVisualEvents().Num();
			bVictimRemoved = World->RemoveEffectByID(VictimID, false);
			FSeinWorldSubsystemTestAccess::SetNextEffectInstanceID(*World, 2);
		})));
		ASSERT_THAT(IsTrue(VictimID > 0));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::RejectedNoMutation));
		ASSERT_THAT(AreEqual(HashBefore, HashAfter));
		ASSERT_THAT(AreEqual(0, NewEventCount));
		ASSERT_THAT(IsTrue(bVictimRemoved));
	}

	TEST(ReplacementProjectionCreditsOnlyTagsOwnedByPlannedVictims,
		"SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), Tag);
		FScopedGrantedTags ExistingGrant(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), Tag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		FScopedGrantedTags ReplacementGrant(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), Tag);
		int64 VictimID = 0;
		int32 CountAfterVictim = 0;
		int32 CountAfterReplacement = 0;
		bool bVictimStillRemovable = true;
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			const FSeinEntityHandle Target =
				SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			FSeinEntityTagState& TagState =
				FSeinWorldSubsystemTestAccess::EntityTags(*World, Target);
			TagState.TagRefCounts.Add(Tag, MAX_int32 - 1);
			TagState.CombinedTags.AddTag(Tag);
			VictimID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			CountAfterVictim = TagState.TagRefCounts.FindChecked(Tag);
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
			CountAfterReplacement = TagState.TagRefCounts.FindChecked(Tag);
			bVictimStillRemovable = World->RemoveEffectByID(VictimID, false);
			World->RemoveEffectByID(Result.EffectInstanceID, false);
			TagState.TagRefCounts.Reset();
			TagState.CombinedTags.Reset();
		})));
		ASSERT_THAT(IsTrue(VictimID > 0));
		ASSERT_THAT(AreEqual(MAX_int32, CountAfterVictim));
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::Applied));
		ASSERT_THAT(IsTrue(Result.EffectInstanceID > VictimID));
		ASSERT_THAT(AreEqual(MAX_int32, CountAfterReplacement));
		ASSERT_THAT(IsFalse(bVictimStillRemovable));
	}

	TEST(EntityTagRefcountSaturationDoesNotOverflow, "SeinARTS.Unit.Effects")
	{
		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FSeinEntityTagState State;
		State.TagRefCounts.Add(Tag, MAX_int32);
		State.CombinedTags.AddTag(Tag);
		ASSERT_THAT(IsFalse(State.GrantTagInternal(Tag)));
		ASSERT_THAT(AreEqual(MAX_int32, State.TagRefCounts.FindChecked(Tag)));
		ASSERT_THAT(IsTrue(State.CombinedTags.HasTagExact(Tag)));
	}

	TEST(TagSaturationCannotCreateUnownedAbilityOrBaseTagTeardown, "SeinARTS.Unit.Effects")
	{
		const FGameplayTag Tag = SeinARTSTags::Environment_Default.GetTag();
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		Assert.ExpectError(TEXT("GrantTag: refcount saturated"), 2);
		Assert.ExpectError(TEXT("ActivateAbility["));
		Assert.ExpectError(TEXT("ReplaceBaseTags: refcount saturated"));
		FSeinEntityHandle Entity;
		int32 AbilityID = INDEX_NONE;
		bool bActivationSucceeded = true;
		bool bAbilityActive = true;
		int32 CommittedTagCount = INDEX_NONE;
		bool bBaseTagAdded = true;
		bool bBaseTagPresentAfterAdd = true;
		bool bBaseTagPresentAfterReplace = true;
		int32 RefCountAfterActivation = 0;
		int32 RefCountAfterAdd = 0;
		int32 RefCountAfterReplace = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Entity = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			World->AddComponent(Entity, FSeinAbilityComponent());
			FSeinEntityTagState& TagState =
				FSeinWorldSubsystemTestAccess::EntityTags(*World, Entity);
			TagState.TagRefCounts.Add(Tag, MAX_int32);
			TagState.CombinedTags.AddTag(Tag);
			AbilityID = USeinAbilityBPFL::SeinGrantAbility(
				World, Entity, USeinEffectLedgerTestAbility::StaticClass());
			USeinAbility* Ability = World->GetAbilityInstance(AbilityID);
			check(Ability);
			Ability->GrantedTags.Reset();
			Ability->GrantedTags.AddTag(Tag);
			bActivationSucceeded =
				Ability->ActivateAbility(Entity, FFixedVector::ZeroVector);
			bAbilityActive = Ability->bIsActive;
			Ability->CancelAbility();
			RefCountAfterActivation = TagState.TagRefCounts.FindChecked(Tag);
			CommittedTagCount = Ability->CommittedGrantedTags.Num();
			bBaseTagAdded = World->AddBaseTag(Entity, Tag);
			bBaseTagPresentAfterAdd = TagState.BaseTags.HasTagExact(Tag);
			RefCountAfterAdd = TagState.TagRefCounts.FindChecked(Tag);
			FGameplayTagContainer Replacement;
			Replacement.AddTag(Tag);
			World->ReplaceBaseTags(Entity, Replacement);
			bBaseTagPresentAfterReplace = TagState.BaseTags.HasTagExact(Tag);
			RefCountAfterReplace = TagState.TagRefCounts.FindChecked(Tag);
			TagState.TagRefCounts.Reset();
			TagState.CombinedTags.Reset();
		})));
		ASSERT_THAT(IsTrue(AbilityID != INDEX_NONE));
		ASSERT_THAT(IsFalse(bActivationSucceeded));
		ASSERT_THAT(IsFalse(bAbilityActive));
		ASSERT_THAT(AreEqual(MAX_int32, RefCountAfterActivation));
		ASSERT_THAT(AreEqual(0, CommittedTagCount));
		ASSERT_THAT(IsFalse(bBaseTagAdded));
		ASSERT_THAT(IsFalse(bBaseTagPresentAfterAdd));
		ASSERT_THAT(AreEqual(MAX_int32, RefCountAfterAdd));
		ASSERT_THAT(IsFalse(bBaseTagPresentAfterReplace));
		ASSERT_THAT(AreEqual(MAX_int32, RefCountAfterReplace));
	}

	TEST(RemovalCallbacksCanInvalidateTheTargetBeforeAReplacementApply, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag RemovedTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(*GetMutableDefault<USeinEffectPeriodicATestEffect>(), RemovedTag);
		FScopedRemoveEffectsWithTag ReplacementRule(
			*GetMutableDefault<USeinEffectPeriodicBTestEffect>(), RemovedTag);
		FSeinEntityHandle Target;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
		})));
		int32 RemovalCallbacks = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName, FSeinEntityHandle CallbackTarget)
		{
			if (Effect.IsA<USeinEffectPeriodicATestEffect>()
				&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
			{
				++RemovalCallbacks;
				World->DestroyEntity(CallbackTarget);
			}
		};

		Assert.ExpectError(TEXT("replacement callbacks invalidated target"));
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
		}
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::InvalidatedAfterReplacementRemoval));
		ASSERT_THAT(AreEqual(int64{0}, Result.EffectInstanceID));
		ASSERT_THAT(AreEqual(1, RemovalCallbacks));
		const FSeinEntity* TargetEntity = World->GetEntityPool().Get(Target);
		ASSERT_THAT(IsNull(TargetEntity));
		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNull(Effects));

		ASSERT_THAT(AreEqual(1, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[0].Type)));
		const int32 TombstoneGeneration =
			World->GetEntityPool().GetSlotGeneration(Target.Index);
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsFalse(TombstoneGeneration
			== World->GetEntityPool().GetSlotGeneration(Target.Index)));
		World->StopSimulation();
	}

	TEST(RemovalCallbackCannotRetargetTheReplacementDefinition,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag RemovedTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), RemovedTag);
		USeinEffect* Replacement =
			GetMutableDefault<USeinEffectPeriodicBTestEffect>();
		ASSERT_THAT(IsNotNull(Replacement));
		TGuardValue<ESeinModifierScope> ScopeGuard(
			Replacement->Scope, ESeinModifierScope::Instance);
		FScopedRemoveEffectsWithTag ReplacementRule(*Replacement, RemovedTag);
		FSeinEntityHandle Target;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
		})));
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle)
			{
				if (Effect.IsA<USeinEffectPeriodicATestEffect>()
					&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
				{
					Replacement->Scope = ESeinModifierScope::Player;
				}
			};

		Assert.ExpectError(TEXT("replacement callbacks invalidated target"));
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
		}
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::InvalidatedAfterReplacementRemoval));
		ASSERT_THAT(AreEqual(int64{0}, Result.EffectInstanceID));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Target)));
		const FSeinActiveEffectsComponent* Active =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Active));
		ASSERT_THAT(AreEqual(0, Active->ActiveEffects.Num()));
		ASSERT_THAT(AreEqual(1, Events.Num()));
		ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::EffectRemoved));
		World->StopSimulation();
	}

	TEST(RemovalCallbackCannotMutateReplacementFutureBehavior,
		"SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FGameplayTag RemovedTag = SeinARTSTags::Environment_Default.GetTag();
		FScopedEffectTag ExistingTag(
			*GetMutableDefault<USeinEffectPeriodicATestEffect>(), RemovedTag);
		USeinEffectPeriodicBTestEffect* Replacement =
			GetMutableDefault<USeinEffectPeriodicBTestEffect>();
		ASSERT_THAT(IsNotNull(Replacement));
		TGuardValue<ESeinModifierScope> ScopeGuard(
			Replacement->Scope, ESeinModifierScope::Instance);
		TGuardValue<FFixedPoint> DerivedBehaviorGuard(
			Replacement->DerivedBehaviorValue, FFixedPoint::Zero);
		FScopedFutureEffectBehavior FutureBehaviorGuard(*Replacement);
		FScopedRemoveEffectsWithTag ReplacementRule(*Replacement, RemovedTag);
		FSeinEntityHandle Target;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			World->FlushVisualEvents();
		})));
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle)
			{
				if (!Effect.IsA<USeinEffectPeriodicATestEffect>()
					|| EventName != GET_FUNCTION_NAME_CHECKED(
						USeinEffect, OnRemoved))
				{
					return;
				}

				Replacement->DefaultTargetClassTag = RemovedTag;
				Replacement->TickInterval = FFixedPoint::One;
				Replacement->bRemoveOnSourceDeath = true;
				FSeinModifier& Modifier = Replacement->Modifiers.AddDefaulted_GetRef();
				Modifier.TargetFieldName = TEXT("CallbackMutatedField");
				Modifier.Operation = ESeinModifierOp::Override;
				Modifier.Value = FFixedPoint::FromInt(7);
				Modifier.Scope = ESeinModifierScope::Class;
				Modifier.TargetClassTag = RemovedTag;
				Replacement->DerivedBehaviorValue = FFixedPoint::FromInt(11);
			};

		Assert.ExpectError(TEXT("replacement callbacks invalidated target"));
		FSeinWorldSubsystemTestAccess::FObservedApplyResult Result;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Result = FSeinWorldSubsystemTestAccess::ApplyEffectDetailed(
				*World, Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
		}
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsTrue(Result.Status == FSeinWorldSubsystemTestAccess::
			EObservedApplyStatus::InvalidatedAfterReplacementRemoval));
		ASSERT_THAT(AreEqual(int64{0}, Result.EffectInstanceID));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Target)));
		const FSeinActiveEffectsComponent* Active =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Active));
		ASSERT_THAT(AreEqual(0, Active->ActiveEffects.Num()));
		ASSERT_THAT(AreEqual(1, Events.Num()));
		ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::EffectRemoved));
		World->StopSimulation();
	}

	TEST(PeriodicEffectDestructionStopsSiblingAndCatchUpTicks, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Target;
		int64 FirstID = 0;
		int64 SecondID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
			FirstID = World->ApplyEffect(
				Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
			SecondID = World->ApplyEffect(
				Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(FirstID > 0 && SecondID > FirstID));

		int32 FirstTicks = 0;
		int32 SiblingTicks = 0;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle CallbackTarget)
		{
			if (EventName != GET_FUNCTION_NAME_CHECKED(USeinEffect, OnTick)) return;
			if (Effect.IsA<USeinEffectPeriodicATestEffect>())
			{
				++FirstTicks;
				World->DestroyEntity(CallbackTarget);
			}
			else if (Effect.IsA<USeinEffectPeriodicBTestEffect>())
			{
				++SiblingTicks;
			}
		};

		ASSERT_THAT(IsTrue(TickForOneSimSecond(*World)));
		ASSERT_THAT(AreEqual(1, FirstTicks));
		ASSERT_THAT(AreEqual(0, SiblingTicks));
		ASSERT_THAT(IsFalse(World->IsEntityAlive(Target)));
	}

	TEST(PlayerEffectTicksUseCanonicalPlayerOrder, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID LaterPlayer(7);
		const FSeinPlayerID EarlierPlayer(2);
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(LaterPlayer, FSeinFactionID(1));
			World->RegisterPlayer(EarlierPlayer, FSeinFactionID(1));
			const FSeinEntityHandle LaterTarget =
				SpawnEffectEntity(*World, LaterPlayer);
			const FSeinEntityHandle EarlierTarget =
				SpawnEffectEntity(*World, EarlierPlayer);
			World->ApplyEffect(LaterTarget,
				USeinEffectIdentityPlayerTestEffect::StaticClass(), LaterTarget);
			World->ApplyEffect(EarlierTarget,
				USeinEffectIdentityPlayerTestEffect::StaticClass(), EarlierTarget);
		})));

		TArray<uint8> CallbackOrder;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook&, FName EventName, FSeinEntityHandle Target)
		{
			if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnTick))
			{
				CallbackOrder.Add(World->GetEntityOwner(Target).Value);
			}
		};

		ASSERT_THAT(IsTrue(TickForOneSimSecond(*World)));
		ASSERT_THAT(AreEqual(2, CallbackOrder.Num()));
		ASSERT_THAT(AreEqual(uint8{2}, CallbackOrder[0]));
		ASSERT_THAT(AreEqual(uint8{7}, CallbackOrder[1]));
	}

	TEST(PlayerEffectExpiryDoesNotDependOnALiveTarget, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(4);
		FSeinEntityHandle Target;
		int64 EffectID = 0;
		ASSERT_THAT(IsTrue(MaterializeEffectFixture(*World, [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Target = SpawnEffectEntity(*World, Player);
			EffectID = World->ApplyEffect(Target,
				USeinEffectTimedPlayerTestEffect::StaticClass(), Target);
		})));
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(Target);
		}

		ASSERT_THAT(IsTrue(TickForOneSimSecond(*World)));
		ASSERT_THAT(IsFalse(World->GetEntityPool().IsValid(Target)));
		const FSeinPlayerState* State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(0, State->PlayerEffects.Num()));
	}
}
