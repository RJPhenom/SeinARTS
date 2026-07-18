#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Containers/Ticker.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Core/SeinSimContext.h"
#include "Data/SeinWorldSnapshot.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
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
	static FSeinEntityTagState& EntityTags(
		USeinWorldSubsystem& World, FSeinEntityHandle Entity)
	{
		return World.EntityTagStates.FindOrAdd(Entity);
	}
};

namespace
{
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
		const FSeinEntityHandle Handle = World.SpawnAbstractEntity(FFixedTransform(), Owner);
		World.AddComponent(Handle, FSeinActiveEffectsComponent());
		return Handle;
	}

	void TickForOneSimSecond(USeinWorldSubsystem& World)
	{
		const int32 TickRate = GetDefault<USeinARTSCoreSettings>()->SimulationTickRate;
		World.StartSimulation();
		// Fixed-point 1/rate truncates, so one extra tick crosses the exact second.
		for (int32 Tick = 0; Tick <= TickRate; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
		}
		World.StopSimulation();
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle First = SpawnEffectEntity(*World, Player);
		const FSeinEntityHandle Second = SpawnEffectEntity(*World, Player);

		const int64 InstanceA = World->ApplyEffect(First,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
		const int64 InstanceB = World->ApplyEffect(Second,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
		const int64 ClassEffect = World->ApplyEffect(First,
			USeinEffectIdentityClassTestEffect::StaticClass(), First);
		const int64 PlayerEffect = World->ApplyEffect(First,
			USeinEffectIdentityPlayerTestEffect::StaticClass(), First);

		ASSERT_THAT(AreEqual(int64{1}, InstanceA));
		ASSERT_THAT(AreEqual(int64{2}, InstanceB));
		ASSERT_THAT(AreEqual(int64{3}, ClassEffect));
		ASSERT_THAT(AreEqual(int64{4}, PlayerEffect));

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
		Assert.ExpectError(TEXT("RestoreSnapshot: unsupported version 1 (expected 4)."));
		ASSERT_THAT(IsFalse(World->RestoreSnapshot(Legacy)));

		FSeinWorldSnapshot DuplicateActiveID = Loaded;
		FSeinPlayerState* DuplicatePlayerState = DuplicateActiveID.PlayerStates.Find(Player);
		ASSERT_THAT(IsNotNull(DuplicatePlayerState));
		ASSERT_THAT(AreEqual(1, DuplicatePlayerState->ClassEffects.Num()));
		ASSERT_THAT(AreEqual(1, DuplicatePlayerState->PlayerEffects.Num()));
		DuplicatePlayerState->PlayerEffects[0].EffectInstanceID =
			DuplicatePlayerState->ClassEffects[0].EffectInstanceID;
		Assert.ExpectError(TEXT("RestoreSnapshot: active effect state is malformed; allocator validation failed."));
		ASSERT_THAT(IsFalse(World->RestoreSnapshot(DuplicateActiveID)));

		FSeinWorldSnapshot ReusingActiveID = Loaded;
		ReusingActiveID.NextEffectInstanceID = 4;
		Assert.ExpectError(TEXT("RestoreSnapshot: next effect ID 4 must exceed max active effect ID 4."));
		ASSERT_THAT(IsFalse(World->RestoreSnapshot(ReusingActiveID)));

		World->EnqueueVisualEvent(FSeinVisualEvent());
		World->GetCollisionSpatialHash().FinishStaticRebuild();
		ASSERT_THAT(IsTrue(World->HasPendingVisualEvents()));
		ASSERT_THAT(IsFalse(World->GetCollisionSpatialHash().IsStaticDirty()));

		ASSERT_THAT(IsTrue(World->RestoreSnapshot(Loaded)));
		World->StopSimulation();
		ASSERT_THAT(IsFalse(World->HasPendingVisualEvents()));
		ASSERT_THAT(IsTrue(World->GetCollisionSpatialHash().IsStaticDirty()));

		const int64 AfterRestore = World->ApplyEffect(First,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), First);
		ASSERT_THAT(AreEqual(int64{5}, AfterRestore));
	}

	TEST(SnapshotAllocatorRejectsAnActiveInstanceEffectID, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		ASSERT_THAT(AreEqual(int64{1}, World->ApplyEffect(
			Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		Snapshot.NextEffectInstanceID = 1;
		const int32 HashBeforeRejectedRestore = World->ComputeStateHash();
		Assert.ExpectError(TEXT("RestoreSnapshot: next effect ID 1 must exceed max active effect ID 1."));
		ASSERT_THAT(IsFalse(World->RestoreSnapshot(Snapshot)));
		ASSERT_THAT(AreEqual(HashBeforeRejectedRestore, World->ComputeStateHash()));
	}

	TEST(SnapshotRestoreDropsAppliesFromTheAbandonedTimeline, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target =
			SpawnEffectEntity(*World, FSeinPlayerID::Neutral());

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		{
			FSeinSimContextScope SimScope;
			ASSERT_THAT(AreEqual(int64{0}, World->ApplyEffect(
				Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target)));
		}
		ASSERT_THAT(AreEqual(0,
			World->GetComponent<FSeinActiveEffectsComponent>(Target)->ActiveEffects.Num()));

		ASSERT_THAT(IsTrue(World->RestoreSnapshot(Snapshot)));
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

		const FSeinEntityHandle First = SpawnEffectEntity(*FirstWorld, FSeinPlayerID::Neutral());
		const FSeinEntityHandle Second = SpawnEffectEntity(*SecondWorld, FSeinPlayerID::Neutral());
		ASSERT_THAT(AreEqual(FirstWorld->ComputeStateHash(), SecondWorld->ComputeStateHash()));

		ASSERT_THAT(AreEqual(int64{1}, FirstWorld->ApplyEffect(First,
			USeinEffectIdentityInstantTestEffect::StaticClass(), First)));
		ASSERT_THAT(IsFalse(FirstWorld->ComputeStateHash() == SecondWorld->ComputeStateHash()));

		ASSERT_THAT(AreEqual(int64{1}, SecondWorld->ApplyEffect(Second,
			USeinEffectIdentityInstantTestEffect::StaticClass(), Second)));
		ASSERT_THAT(AreEqual(FirstWorld->ComputeStateHash(), SecondWorld->ComputeStateHash()));
	}

	TEST(EffectCallbacksMayRemoveSiblingEffectsSafely, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());

		const int64 FirstID = World->ApplyEffect(Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
		const int64 SecondID = World->ApplyEffect(Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);
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

		TickForOneSimSecond(*World);
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		const int64 FirstID = World->ApplyEffect(Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
		const int64 SecondID = World->ApplyEffect(Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target);

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

		ASSERT_THAT(IsTrue(World->RemoveEffect(Target, FirstID, false)));
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->FlushVisualEvents();

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

		const int64 AppliedID = World->ApplyEffect(
			Target, USeinEffectPeriodicATestEffect::StaticClass(), Target);
		ASSERT_THAT(IsTrue(bSawActiveInstance));
		ASSERT_THAT(IsTrue(bRemovalSucceeded));
		ASSERT_THAT(AreEqual(AppliedID, RemovedID));
		ASSERT_THAT(IsFalse(World->RemoveEffectByID(AppliedID, /*bByExpiration=*/false)));

		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectApplied),
			static_cast<uint8>(Events[0].Type)));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[1].Type)));
	}

	TEST(InstantEffectDestroyedTargetStillCompletesRemovalLifecycle, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target = SpawnEffectEntity(
			*World, FSeinPlayerID::Neutral());
		World->FlushVisualEvents();

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

		const int64 EffectID = World->ApplyEffect(Target,
			USeinEffectIdentityInstantTestEffect::StaticClass(), Target);
		ASSERT_THAT(IsTrue(EffectID > 0));
		ASSERT_THAT(AreEqual(1, ApplyCalls));
		ASSERT_THAT(AreEqual(1, RemovedCalls));
		ASSERT_THAT(IsFalse(World->IsEntityAlive(Target)));
		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(0, Effects->ActiveEffects.Num()));

		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectApplied),
			static_cast<uint8>(Events[0].Type)));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[1].Type)));
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, Player);
		const int64 InstanceID = World->ApplyEffect(
			Target, USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		const int64 ClassID = World->ApplyEffect(
			Target, USeinEffectIdentityClassTestEffect::StaticClass(), Target);
		const int64 PlayerID = World->ApplyEffect(
			Target, USeinEffectIdentityPlayerTestEffect::StaticClass(), Target);

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

		ASSERT_THAT(IsTrue(World->RemoveEffectByID(ClassID, /*bByExpiration=*/false)));
		ASSERT_THAT(IsFalse(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Class, QueryTag)));
		ASSERT_THAT(IsTrue(World->HasEffectWithTagForPlayer(Player, ESeinModifierScope::Player, QueryTag)));
		ASSERT_THAT(IsTrue(World->RemoveEffectByID(PlayerID, /*bByExpiration=*/false)));
		ASSERT_THAT(IsTrue(World->RemoveEffectByID(InstanceID, /*bByExpiration=*/false)));
		ASSERT_THAT(IsFalse(World->HasInstanceEffectWithTag(Target, QueryTag)));
		ASSERT_THAT(IsFalse(World->RemoveEffectByID(InstanceID, /*bByExpiration=*/false)));
	}

	TEST(PassiveGrantRemovalSkipsOnApplyAndPublishesNoDeadAbilityID, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());
		World->FlushVisualEvents();

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

		const int64 AssignedID = World->ApplyEffect(
			Target, USeinEffectPassiveGrantTestEffect::StaticClass(), Target);
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());

		ASSERT_THAT(IsTrue(USeinAbilityBPFL::SeinGrantAbility(
			World, Target, USeinEffectLedgerTestAbility::StaticClass()) != INDEX_NONE));
		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));

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

		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectPassiveGrantTestEffect::StaticClass(), Target) > 0));
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());
		const int64 EffectID = World->ApplyEffect(Target,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		ASSERT_THAT(IsTrue(EffectID > 0));

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

		ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		ASSERT_THAT(IsTrue(World->RestoreSnapshot(Loaded)));
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

		ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
		const int64 FirstID = World->ApplyEffect(Driver,
			USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		const int64 LaterID = World->ApplyEffect(Driver,
			USeinEffectIdentityPlayerTestEffect::StaticClass(), Driver);
		ASSERT_THAT(IsTrue(FirstID > 0 && LaterID > FirstID));

		bool bRemovedFirst = false;
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle)
		{
			bRemovedFirst = World->RemoveEffectByID(FirstID, false);
		};
		const FSeinEntityHandle Recipient = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		const int64 EffectID = World->ApplyEffect(Target,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(1, Effects->ActiveEffects.Num()));
		Effects->ActiveEffects[0].CurrentStacks = MAX_int32;

		ASSERT_THAT(AreEqual(EffectID, World->ApplyEffect(Target,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), Target)));
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		const FSeinEntityHandle Alternate = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());
		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), Target) > 0));

		FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(1, Effects->ActiveEffects[0].CommittedAbilityGrants.Num()));
		const int32 Before = World->ComputeStateHash();
		Effects->ActiveEffects[0].CommittedAbilityGrants[0].Recipient = Alternate;
		ASSERT_THAT(IsFalse(Before == World->ComputeStateHash()));
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());
		const int64 EffectID = World->ApplyEffect(Target,
			USeinEffectIdentityInstanceTestEffect::StaticClass(), Target);
		ASSERT_THAT(IsTrue(EffectID > 0));

		ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinForceRevokeAbilityByClass(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
		ASSERT_THAT(IsTrue(USeinAbilityBPFL::SeinGrantAbility(
			World, Target, USeinEffectLedgerTestAbility::StaticClass()) != INDEX_NONE));
		ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
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
		World->RegisterPlayer(PlayerA, FSeinFactionID(1));
		World->RegisterPlayer(PlayerB, FSeinFactionID(1));
		const FSeinEntityHandle DriverA = SpawnEffectEntity(*World, PlayerA);
		const FSeinEntityHandle DriverB = SpawnEffectEntity(*World, PlayerB);
		const int64 EffectA = World->ApplyEffect(DriverA,
			USeinEffectIdentityClassTestEffect::StaticClass(), DriverA);
		const int64 EffectB = World->ApplyEffect(DriverB,
			USeinEffectIdentityClassTestEffect::StaticClass(), DriverB);
		ASSERT_THAT(IsTrue(EffectA > 0 && EffectB > EffectA));
		const FSeinEntityHandle Recipient = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerA);
		ASSERT_THAT(IsTrue(USeinAbilityBPFL::SeinGrantAbility(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass()) != INDEX_NONE));
		ASSERT_THAT(AreEqual(2, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));

		{
			FSeinSimContextScope SimScope;
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
		World->RegisterPlayer(PlayerA, FSeinFactionID(1));
		World->RegisterPlayer(PlayerB, FSeinFactionID(1));
		World->RegisterPlayer(PlayerC, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, PlayerB);
		const int64 EffectID = World->ApplyEffect(Driver,
			USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		const FSeinEntityHandle Recipient = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerA);

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
		{
			FSeinSimContextScope SimScope;
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
		ASSERT_THAT(IsTrue(World->RestoreSnapshot(Snapshot)));
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
		World->RegisterPlayer(PlayerA, FSeinFactionID(1));
		World->RegisterPlayer(PlayerB, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, PlayerB);
		const int64 EffectID = World->ApplyEffect(Driver,
			USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		const FSeinEntityHandle Recipient = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), PlayerB);
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
		{
			FSeinSimContextScope SimScope;
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
		ASSERT_THAT(IsTrue(World->RestoreSnapshot(Snapshot)));
		ASSERT_THAT(IsTrue(World->RemoveEffectByID(EffectID, false)));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectRemovingPassiveTestAbility::StaticClass())));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Recipient, USeinEffectLedgerTestAbility::StaticClass())));
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
		const FSeinEntityHandle First = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
		const FSeinEntityHandle Second = World->SpawnEntity(
			ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
		USeinEffectRemovingPassiveTestAbility::ActivationCallback =
			[&](FSeinEntityHandle Owner)
		{
			if (Owner == First)
			{
				World->UngrantTag(Owner, ClassTag);
			}
		};
		ASSERT_THAT(IsTrue(World->ApplyEffect(Driver,
			USeinEffectIdentityClassTestEffect::StaticClass(), Driver) > 0));

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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		World->AddComponent(Target, FSeinAbilityComponent());
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
		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectPassiveGrantTestEffect::StaticClass(), Target) > 0));
		ASSERT_THAT(AreEqual(0, ApplyCallbacks));
		ASSERT_THAT(AreEqual(0, USeinAbilityBPFL::SeinGetAbilityGrantCount(
			World, Target, USeinEffectLedgerTestAbility::StaticClass())));
	}

	TEST(DestroyQueuePreservesEntitiesQueuedByCallbacks, "SeinARTS.Unit.Effects")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle First = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		const FSeinEntityHandle Second = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		const FDelegateHandle Callback = World->OnEntityDestroyed.AddLambda(
			[&](FSeinEntityHandle Destroyed)
			{
				if (Destroyed == First) World->DestroyEntity(Second);
			});
		World->DestroyEntity(First);
		World->StartSimulation();
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();
		ASSERT_THAT(IsNull(World->GetEntityPool().Get(First)));
		ASSERT_THAT(IsNotNull(World->GetEntityPool().Get(Second)));
		ASSERT_THAT(IsFalse(World->GetEntityPool().Get(Second)->IsAlive()));
		World->StartSimulation();
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
		ASSERT_THAT(IsTrue(World->ApplyEffect(Driver,
			USeinEffectIdentityClassTestEffect::StaticClass(), Driver) > 0));
		for (int32 Iteration = 0; Iteration < 3; ++Iteration)
		{
			const FSeinEntityHandle Recipient = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
			ASSERT_THAT(AreEqual(1,
				World->GetPlayerState(Player)->ClassEffects[0].CommittedAbilityGrants.Num()));
			World->DestroyEntity(Recipient);
			World->StartSimulation();
			FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
			World->StopSimulation();
			ASSERT_THAT(AreEqual(0,
				World->GetPlayerState(Player)->ClassEffects[0].CommittedAbilityGrants.Num()));
		}
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Driver = SpawnEffectEntity(*World, Player);
		World->ApplyEffect(Driver, USeinEffectIdentityClassTestEffect::StaticClass(), Driver);
		World->SpawnEntity(ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
		FSeinWorldSnapshot Valid;
		World->CaptureSnapshot(Valid);
		const int32 HashBefore = World->ComputeStateHash();

		auto ExpectRejectedWithoutMutation = [&](FSeinWorldSnapshot& Bad)
		{
			Assert.ExpectError(TEXT("RestoreSnapshot: active effect state is malformed; allocator validation failed."));
			ASSERT_THAT(IsFalse(World->RestoreSnapshot(Bad)));
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
		const FSeinEntityHandle Entity = World->SpawnAbstractEntity(
			FFixedTransform(), FSeinPlayerID::Neutral());
		World->AddComponent(Entity, FSeinAbilityComponent());
		FSeinEntityTagState& TagState =
			FSeinWorldSubsystemTestAccess::EntityTags(*World, Entity);
		TagState.TagRefCounts.Add(Tag, MAX_int32);
		TagState.CombinedTags.AddTag(Tag);

		const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
			World, Entity, USeinEffectLedgerTestAbility::StaticClass());
		ASSERT_THAT(IsTrue(AbilityID != INDEX_NONE));
		USeinAbility* Ability = World->GetAbilityInstance(AbilityID);
		ASSERT_THAT(IsNotNull(Ability));
		Ability->GrantedTags.Reset();
		Ability->GrantedTags.AddTag(Tag);
		Assert.ExpectError(TEXT("GrantTag: refcount saturated"));
		Assert.ExpectError(TEXT("ActivateAbility["));
		ASSERT_THAT(IsFalse(Ability->ActivateAbility(Entity, FFixedVector::ZeroVector)));
		ASSERT_THAT(IsFalse(Ability->bIsActive));
		Ability->CancelAbility();
		ASSERT_THAT(AreEqual(MAX_int32, TagState.TagRefCounts.FindChecked(Tag)));
		ASSERT_THAT(AreEqual(0, Ability->CommittedGrantedTags.Num()));

		Assert.ExpectError(TEXT("GrantTag: refcount saturated"));
		ASSERT_THAT(IsFalse(World->AddBaseTag(Entity, Tag)));
		ASSERT_THAT(IsFalse(TagState.BaseTags.HasTagExact(Tag)));
		ASSERT_THAT(AreEqual(MAX_int32, TagState.TagRefCounts.FindChecked(Tag)));
		FGameplayTagContainer Replacement;
		Replacement.AddTag(Tag);
		Assert.ExpectError(TEXT("ReplaceBaseTags: refcount saturated"));
		World->ReplaceBaseTags(Entity, Replacement);
		ASSERT_THAT(IsFalse(TagState.BaseTags.HasTagExact(Tag)));
		ASSERT_THAT(AreEqual(MAX_int32, TagState.TagRefCounts.FindChecked(Tag)));
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
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(World->ApplyEffect(
			Target, USeinEffectPeriodicATestEffect::StaticClass(), Target) > 0));
		World->FlushVisualEvents();

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

		ASSERT_THAT(AreEqual(int64{0}, World->ApplyEffect(
			Target, USeinEffectPeriodicBTestEffect::StaticClass(), Target)));
		ASSERT_THAT(AreEqual(1, RemovalCallbacks));
		const FSeinEntity* TargetEntity = World->GetEntityPool().Get(Target);
		ASSERT_THAT(IsNotNull(TargetEntity));
		ASSERT_THAT(IsFalse(TargetEntity->IsAlive()));
		const FSeinActiveEffectsComponent* Effects =
			World->GetComponent<FSeinActiveEffectsComponent>(Target);
		ASSERT_THAT(IsNotNull(Effects));
		ASSERT_THAT(AreEqual(0, Effects->ActiveEffects.Num()));

		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(AreEqual(1, Events.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint8>(ESeinVisualEventType::EffectRemoved),
			static_cast<uint8>(Events[0].Type)));
	}

	TEST(PeriodicEffectDestructionStopsSiblingAndCatchUpTicks, "SeinARTS.Unit.Effects")
	{
		FScopedEffectCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinEntityHandle Target = SpawnEffectEntity(
			*World, FSeinPlayerID::Neutral());
		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectPeriodicATestEffect::StaticClass(), Target) > 0));
		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectPeriodicBTestEffect::StaticClass(), Target) > 0));

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

		TickForOneSimSecond(*World);
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
		World->RegisterPlayer(LaterPlayer, FSeinFactionID(1));
		World->RegisterPlayer(EarlierPlayer, FSeinFactionID(1));
		const FSeinEntityHandle LaterTarget = SpawnEffectEntity(*World, LaterPlayer);
		const FSeinEntityHandle EarlierTarget = SpawnEffectEntity(*World, EarlierPlayer);
		World->ApplyEffect(LaterTarget, USeinEffectIdentityPlayerTestEffect::StaticClass(), LaterTarget);
		World->ApplyEffect(EarlierTarget, USeinEffectIdentityPlayerTestEffect::StaticClass(), EarlierTarget);

		TArray<uint8> CallbackOrder;
		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook&, FName EventName, FSeinEntityHandle Target)
		{
			if (EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnTick))
			{
				CallbackOrder.Add(World->GetEntityOwner(Target).Value);
			}
		};

		TickForOneSimSecond(*World);
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
		World->RegisterPlayer(Player, FSeinFactionID(1));
		const FSeinEntityHandle Target = SpawnEffectEntity(*World, Player);
		ASSERT_THAT(IsTrue(World->ApplyEffect(Target,
			USeinEffectTimedPlayerTestEffect::StaticClass(), Target) > 0));
		World->DestroyEntity(Target);

		TickForOneSimSecond(*World);
		ASSERT_THAT(IsFalse(World->GetEntityPool().IsValid(Target)));
		const FSeinPlayerState* State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(0, State->PlayerEffects.Num()));
	}
}
