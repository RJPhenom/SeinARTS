#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinActor.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinWeaponComponent.h"
#include "Algo/Reverse.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinComponentLiveTuning.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"
#include "TestTypes/SeinPlacementYawTestTypes.h"
#include "TestTypes/SeinProductionCostTestTypes.h"
#include "TestTypes/SeinSnapshotValidationTestTypes.h"
#include "UObject/UnrealType.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinComponentPropertyPatch MakeTopSpeedPatch(
			const FSeinMovementComponent& Before,
			FFixedPoint NewTopSpeed,
			ESeinComponentInstanceOverrideOperation OverrideOperation)
		{
			FSeinMovementComponent After = Before;
			After.TopSpeed = NewTopSpeed;
			TArray<FInstancedStruct> BeforeEntries;
			BeforeEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinMovementComponent>(Before);
			TArray<FInstancedStruct> AfterEntries;
			AfterEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinMovementComponent>(After);
			TArray<FSeinComponentPropertyPatch> Patches;
			FString Error;
			const bool bBuilt = SeinBuildComponentPropertyPatches(
				BeforeEntries, AfterEntries, Patches, Error);
			checkf(bBuilt && Patches.Num() == 1, TEXT("%s"), *Error);
			Patches[0].InstanceOverrideOperation = OverrideOperation;
			return Patches[0];
		}

		FSeinCommand MakeLiveTuningCommand(
			const FSeinComponentLiveTuningRequest& Payload,
			FSeinPlayerID Player)
		{
			FSeinCommand Command;
			Command.PlayerID = Player;
			Command.CommandType =
				SeinARTSTags::Command_Type_Editor_ComponentPropertyPatch;
			Command.SchemaVersion = 1;
			Command.EntityHandle = Payload.TargetEntity;
			FSeinComponentLiveTuningCommandPayload WirePayload;
			FString Error;
			checkf(SeinEncodeComponentLiveTuningRequest(
				Payload, WirePayload, Error), TEXT("%s"), *Error);
			Command.Payload.InitializeAs<
				FSeinComponentLiveTuningCommandPayload>(WirePayload);
			return Command;
		}

		/** Every authored difference between two movement payloads, as patches. */
		TArray<FSeinComponentPropertyPatch> MakeMovementPatches(
			const FSeinMovementComponent& Before,
			const FSeinMovementComponent& After,
			ESeinComponentInstanceOverrideOperation OverrideOperation)
		{
			TArray<FInstancedStruct> BeforeEntries;
			BeforeEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinMovementComponent>(Before);
			TArray<FInstancedStruct> AfterEntries;
			AfterEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinMovementComponent>(After);
			TArray<FSeinComponentPropertyPatch> Patches;
			FString Error;
			checkf(SeinBuildComponentPropertyPatches(
				BeforeEntries, AfterEntries, Patches, Error), TEXT("%s"), *Error);
			for (FSeinComponentPropertyPatch& Patch : Patches)
			{
				Patch.InstanceOverrideOperation = OverrideOperation;
			}
			return Patches;
		}

		/** True while the override set holds the strictly increasing
		 *  (Entity, PatchKey) order RestoreSnapshot re-validates. */
		bool IsCanonicalOverrideOrder(
			TConstArrayView<FSeinComponentEntityOverrideRecord> Records)
		{
			for (int32 Index = 1; Index < Records.Num(); ++Index)
			{
				const FSeinComponentEntityOverrideRecord& Previous =
					Records[Index - 1];
				const FSeinComponentEntityOverrideRecord& Current = Records[Index];
				if (Previous.Entity == Current.Entity)
				{
					if (SeinMakeComponentPropertyPatchKey(
							Current.ComponentTypePath, Current.PropertyPath)
						<= SeinMakeComponentPropertyPatchKey(
							Previous.ComponentTypePath, Previous.PropertyPath))
					{
						return false;
					}
				}
				else if (Current.Entity < Previous.Entity)
				{
					return false;
				}
			}
			return true;
		}

		FSeinComponentPropertyPatch MakeGrantedAbilitiesPatch(
			const FSeinAbilityComponent& Before,
			TArray<TSubclassOf<USeinAbility>> NewAbilities,
			ESeinComponentInstanceOverrideOperation OverrideOperation)
		{
			FSeinAbilityComponent After = Before;
			After.GrantedAbilities = MoveTemp(NewAbilities);
			TArray<FInstancedStruct> BeforeEntries;
			BeforeEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinAbilityComponent>(Before);
			TArray<FInstancedStruct> AfterEntries;
			AfterEntries.AddDefaulted_GetRef()
				.InitializeAs<FSeinAbilityComponent>(After);
			TArray<FSeinComponentPropertyPatch> Patches;
			FString Error;
			checkf(SeinBuildComponentPropertyPatches(
				BeforeEntries, AfterEntries, Patches, Error)
				&& Patches.Num() == 1, TEXT("%s"), *Error);
			Patches[0].InstanceOverrideOperation = OverrideOperation;
			return Patches[0];
		}
	}

	TEST(ComponentLiveTuningTraversesConcreteInstancedStructFields,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FSeinMovementComponent Before;
		FSeinAvoidanceOutput AuthoredTuning;
		AuthoredTuning.SteerDir = FFixedVector(
			FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
		AuthoredTuning.SpeedScale = FFixedPoint::One;
		Before.MovementClassData.InitializeAs<FSeinAvoidanceOutput>(
			AuthoredTuning);

		FSeinMovementComponent After = Before;
		After.MovementClassData.GetMutable<FSeinAvoidanceOutput>()
			.SpeedScale = FFixedPoint::Two;
		TArray<FInstancedStruct> BeforeEntries;
		BeforeEntries.AddDefaulted_GetRef()
			.InitializeAs<FSeinMovementComponent>(Before);
		TArray<FInstancedStruct> AfterEntries;
		AfterEntries.AddDefaulted_GetRef()
			.InitializeAs<FSeinMovementComponent>(After);

		TArray<FSeinComponentPropertyPatch> Patches;
		FString Error;
		ASSERT_THAT(IsTrue(SeinBuildComponentPropertyPatches(
			BeforeEntries, AfterEntries, Patches, Error)));
		ASSERT_THAT(AreEqual(1, Patches.Num()));
		ASSERT_THAT(IsTrue(Patches[0].PropertyPath.Num() >= 2));
		ASSERT_THAT(AreEqual(
			GET_MEMBER_NAME_STRING_CHECKED(
				FSeinMovementComponent, MovementClassData),
			Patches[0].PropertyPath[0].PropertyName));

		FSeinMovementComponent RuntimeValue = Before;
		const FFixedVector RuntimeSteer(
			FFixedPoint::FromInt(9), FFixedPoint::FromInt(4),
			FFixedPoint::Zero);
		const FFixedVector RuntimeVelocity(
			FFixedPoint::FromInt(13), FFixedPoint::FromInt(7),
			FFixedPoint::Zero);
		RuntimeValue.MovementClassData.GetMutable<FSeinAvoidanceOutput>()
			.SteerDir = RuntimeSteer;
		RuntimeValue.Velocity = RuntimeVelocity;

		FProperty* Property = nullptr;
		void* Value = nullptr;
		ASSERT_THAT(IsTrue(SeinResolveComponentPropertyPath(
			FSeinMovementComponent::StaticStruct(), &RuntimeValue,
			Patches[0].PropertyPath, Property, Value, Error)));
		const TCHAR* End = Property->ImportText_Direct(
			*Patches[0].ExportedValue, Value, nullptr, PPF_None);
		ASSERT_THAT(IsNotNull(End));
		ASSERT_THAT(IsTrue(*End == TEXT('\0')));
		const FSeinAvoidanceOutput& Tuned =
			RuntimeValue.MovementClassData.Get<FSeinAvoidanceOutput>();
		ASSERT_THAT(IsTrue(Tuned.SpeedScale == FFixedPoint::Two));
		ASSERT_THAT(IsTrue(Tuned.SteerDir == RuntimeSteer));
		ASSERT_THAT(IsTrue(RuntimeValue.Velocity == RuntimeVelocity));
	}

	TEST(ComponentLiveTuningPreservesRuntimeStateInsideAuthoredArrays,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FSeinWeaponComponent Before;
		FSeinWeaponSlot& BeforeSlot = Before.Weapons.AddDefaulted_GetRef();
		BeforeSlot.Range = FFixedPoint::FromInt(1000);
		FSeinWeaponComponent After = Before;
		After.Weapons[0].Range = FFixedPoint::FromInt(1400);

		TArray<FInstancedStruct> BeforeEntries;
		BeforeEntries.AddDefaulted_GetRef()
			.InitializeAs<FSeinWeaponComponent>(Before);
		TArray<FInstancedStruct> AfterEntries;
		AfterEntries.AddDefaulted_GetRef()
			.InitializeAs<FSeinWeaponComponent>(After);
		TArray<FSeinComponentPropertyPatch> Patches;
		FString Error;
		ASSERT_THAT(IsTrue(SeinBuildComponentPropertyPatches(
			BeforeEntries, AfterEntries, Patches, Error)));
		ASSERT_THAT(AreEqual(1, Patches.Num()));
		ASSERT_THAT(IsTrue(Patches[0].PropertyPath.Num() >= 2));
		ASSERT_THAT(AreEqual(0, Patches[0].PropertyPath[0].ArrayIndex));

		FSeinWeaponComponent RuntimeValue = Before;
		RuntimeValue.bRuntimeSeeded = true;
		RuntimeValue.Weapons[0].CooldownRemaining = FFixedPoint::FromInt(3);
		RuntimeValue.Weapons[0].ReloadRemaining = FFixedPoint::FromInt(7);
		RuntimeValue.Weapons[0].MagazineRemaining = 11;
		FProperty* Property = nullptr;
		void* Value = nullptr;
		ASSERT_THAT(IsTrue(SeinResolveComponentPropertyPath(
			FSeinWeaponComponent::StaticStruct(), &RuntimeValue,
			Patches[0].PropertyPath, Property, Value, Error)));
		const TCHAR* End = Property->ImportText_Direct(
			*Patches[0].ExportedValue, Value, nullptr, PPF_None);
		ASSERT_THAT(IsNotNull(End));
		ASSERT_THAT(IsTrue(*End == TEXT('\0')));
		ASSERT_THAT(IsTrue(
			RuntimeValue.Weapons[0].Range == FFixedPoint::FromInt(1400)));
		ASSERT_THAT(IsTrue(RuntimeValue.bRuntimeSeeded));
		ASSERT_THAT(IsTrue(RuntimeValue.Weapons[0].CooldownRemaining
			== FFixedPoint::FromInt(3)));
		ASSERT_THAT(IsTrue(RuntimeValue.Weapons[0].ReloadRemaining
			== FFixedPoint::FromInt(7)));
		ASSERT_THAT(AreEqual(11, RuntimeValue.Weapons[0].MagazineRemaining));

		After.Weapons.AddDefaulted();
		AfterEntries[0].InitializeAs<FSeinWeaponComponent>(After);
		Patches.Reset();
		Error.Reset();
		ASSERT_THAT(IsFalse(SeinBuildComponentPropertyPatches(
			BeforeEntries, AfterEntries, Patches, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("runtime-only state"))));
	}

	TEST(ComponentLiveTuningPreservesInstanceOverridesAndRuntimeState,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle InheritedEntity;
		FSeinEntityHandle OverriddenEntity;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			InheritedEntity = World->SpawnEntity(
				ASeinActor::StaticClass(), FFixedTransform(), Player);
			OverriddenEntity = World->SpawnEntity(
				ASeinActor::StaticClass(), FFixedTransform(), Player);

			FSeinMovementComponent InheritedMovement;
			InheritedMovement.TopSpeed = FFixedPoint::FromInt(500);
			InheritedMovement.Velocity = FFixedVector(
				FFixedPoint::FromInt(17), FFixedPoint::FromInt(3),
				FFixedPoint::Zero);
			World->AddComponent(InheritedEntity, InheritedMovement);

			FSeinMovementComponent OverriddenMovement = InheritedMovement;
			OverriddenMovement.Velocity = FFixedVector(
				FFixedPoint::FromInt(29), FFixedPoint::FromInt(5),
				FFixedPoint::Zero);
			World->AddComponent(OverriddenEntity, OverriddenMovement);
			World->AddComponent(
				OverriddenEntity, FSeinAbilityComponent());
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0,
			TEXT("SeinARTS.ComponentLiveTuning.PropertyOverlay"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const FSeinMovementComponent InheritedBefore =
			*World->GetComponent<FSeinMovementComponent>(InheritedEntity);
		const FSeinMovementComponent OverriddenBefore =
			*World->GetComponent<FSeinMovementComponent>(OverriddenEntity);

		FSeinComponentLiveTuningRequest EntityPayload;
		EntityPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		EntityPayload.TargetEntity = OverriddenEntity;
		EntityPayload.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		EntityPayload.Patches.Add(MakeTopSpeedPatch(
			OverriddenBefore, FFixedPoint::FromInt(700),
			ESeinComponentInstanceOverrideOperation::Set));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(EntityPayload, Player), true)));
		ASSERT_THAT(IsTrue(
			World->GetComponent<FSeinMovementComponent>(OverriddenEntity)
				->TopSpeed == FFixedPoint::FromInt(500)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());

		const FSeinMovementComponent* OverriddenAfterEntity =
			World->GetComponent<FSeinMovementComponent>(OverriddenEntity);
		ASSERT_THAT(IsNotNull(OverriddenAfterEntity));
		ASSERT_THAT(IsTrue(
			OverriddenAfterEntity->TopSpeed == FFixedPoint::FromInt(700)));
		ASSERT_THAT(IsTrue(
			OverriddenAfterEntity->Velocity == OverriddenBefore.Velocity));

		const FSeinAbilityComponent EmptyAbilityComponent =
			*World->GetComponent<FSeinAbilityComponent>(OverriddenEntity);
		bool bRuntimeAbilityGranted = false;
		const FDelegateHandle RuntimeGrantHandle =
			World->OnComponentPropertyLiveTuned.AddLambda(
				[&](FSeinEntityHandle Entity, const UScriptStruct& Type,
					const FSeinComponentPropertyPatch& Patch)
				{
					if (!bRuntimeAbilityGranted && Entity == OverriddenEntity
						&& &Type == FSeinAbilityComponent::StaticStruct()
						&& !Patch.PropertyPath.IsEmpty()
						&& Patch.PropertyPath[0].PropertyName
							== GET_MEMBER_NAME_STRING_CHECKED(
								FSeinAbilityComponent, GrantedAbilities))
					{
						bRuntimeAbilityGranted = true;
						USeinAbilityBPFL::SeinGrantAbility(
							World, Entity,
							USeinSnapshotWithinTestAbility::StaticClass());
					}
				});
		FSeinComponentLiveTuningRequest GrantAbilityPayload;
		GrantAbilityPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		GrantAbilityPayload.TargetEntity = OverriddenEntity;
		GrantAbilityPayload.ActorClassPath =
			ASeinActor::StaticClass()->GetPathName();
		GrantAbilityPayload.Patches.Add(MakeGrantedAbilitiesPatch(
			EmptyAbilityComponent,
			{USeinSnapshotTestAbility::StaticClass()},
			ESeinComponentInstanceOverrideOperation::Set));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(GrantAbilityPayload, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		const FSeinAbilityComponent* GrantedAbilityComponent =
			World->GetComponent<FSeinAbilityComponent>(OverriddenEntity);
		ASSERT_THAT(IsTrue(bRuntimeAbilityGranted));
		ASSERT_THAT(AreEqual(2, GrantedAbilityComponent->GrantedAbilities.Num()));
		ASSERT_THAT(AreEqual(2, GrantedAbilityComponent->AbilityInstanceIDs.Num()));
		ASSERT_THAT(IsTrue(GrantedAbilityComponent->HasAbilityOfClass(
			*World, USeinSnapshotTestAbility::StaticClass())));
		ASSERT_THAT(IsTrue(GrantedAbilityComponent->HasAbilityOfClass(
			*World, USeinSnapshotWithinTestAbility::StaticClass())));

		FSeinComponentLiveTuningRequest RevokeAbilityPayload;
		RevokeAbilityPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		RevokeAbilityPayload.TargetEntity = OverriddenEntity;
		RevokeAbilityPayload.ActorClassPath =
			ASeinActor::StaticClass()->GetPathName();
		RevokeAbilityPayload.Patches.Add(MakeGrantedAbilitiesPatch(
			*GrantedAbilityComponent, {},
			ESeinComponentInstanceOverrideOperation::Clear));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(RevokeAbilityPayload, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		GrantedAbilityComponent =
			World->GetComponent<FSeinAbilityComponent>(OverriddenEntity);
		ASSERT_THAT(AreEqual(1, GrantedAbilityComponent->GrantedAbilities.Num()));
		ASSERT_THAT(AreEqual(1, GrantedAbilityComponent->AbilityInstanceIDs.Num()));
		ASSERT_THAT(IsFalse(GrantedAbilityComponent->HasAbilityOfClass(
			*World, USeinSnapshotTestAbility::StaticClass())));
		ASSERT_THAT(IsTrue(GrantedAbilityComponent->HasAbilityOfClass(
			*World, USeinSnapshotWithinTestAbility::StaticClass())));
		World->OnComponentPropertyLiveTuned.Remove(RuntimeGrantHandle);

		FSeinComponentLiveTuningRequest ClassPayload;
		ClassPayload.Scope = ESeinComponentLiveTuningScope::ActorClass;
		ClassPayload.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		ClassPayload.Patches.Add(MakeTopSpeedPatch(
			InheritedBefore, FFixedPoint::FromInt(900),
			ESeinComponentInstanceOverrideOperation::None));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(ClassPayload, Player), true)));
		ASSERT_THAT(IsTrue(
			World->GetComponent<FSeinMovementComponent>(InheritedEntity)
				->TopSpeed == FFixedPoint::FromInt(500)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());

		const FSeinMovementComponent* InheritedAfterClass =
			World->GetComponent<FSeinMovementComponent>(InheritedEntity);
		OverriddenAfterEntity =
			World->GetComponent<FSeinMovementComponent>(OverriddenEntity);
		ASSERT_THAT(IsTrue(
			InheritedAfterClass->TopSpeed == FFixedPoint::FromInt(900)));
		ASSERT_THAT(IsTrue(
			InheritedAfterClass->Velocity == InheritedBefore.Velocity));
		ASSERT_THAT(IsTrue(
			OverriddenAfterEntity->TopSpeed == FFixedPoint::FromInt(700)));
		ASSERT_THAT(IsTrue(
			OverriddenAfterEntity->Velocity == OverriddenBefore.Velocity));

		FSeinComponentLiveTuningRequest ClearPayload;
		ClearPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		ClearPayload.TargetEntity = OverriddenEntity;
		ClearPayload.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		ClearPayload.Patches.Add(MakeTopSpeedPatch(
			*OverriddenAfterEntity, FFixedPoint::FromInt(900),
			ESeinComponentInstanceOverrideOperation::Clear));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(ClearPayload, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(
			World->GetComponent<FSeinMovementComponent>(OverriddenEntity)
				->TopSpeed == FFixedPoint::FromInt(900)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		const int32 SnapshotStateHash = World->ComputeStateHash();
		ASSERT_THAT(AreEqual(
			1, Snapshot.ComponentLiveTuningClassDefaults.Num()));
		ASSERT_THAT(AreEqual(
			0, Snapshot.ComponentLiveTuningEntityOverrides.Num()));
		ASSERT_THAT(AreEqual(
			1, Snapshot.ComponentLiveTuningAuthoredAbilityGrants.Num()));
		ASSERT_THAT(AreEqual(
			0, Snapshot.ComponentLiveTuningAuthoredAbilityGrants[0]
				.AuthoredAbilities.Num()));

		FSeinComponentLiveTuningRequest PostSnapshotPayload;
		PostSnapshotPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		PostSnapshotPayload.TargetEntity = OverriddenEntity;
		PostSnapshotPayload.ActorClassPath =
			ASeinActor::StaticClass()->GetPathName();
		PostSnapshotPayload.Patches.Add(MakeTopSpeedPatch(
			*World->GetComponent<FSeinMovementComponent>(OverriddenEntity),
			FFixedPoint::FromInt(1100),
			ESeinComponentInstanceOverrideOperation::Set));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(PostSnapshotPayload, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(World->ComputeStateHash() != SnapshotStateHash));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, Snapshot)));
		ASSERT_THAT(AreEqual(SnapshotStateHash, World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(
			World->GetComponent<FSeinMovementComponent>(OverriddenEntity)
				->TopSpeed == FFixedPoint::FromInt(900)));
		ASSERT_THAT(AreEqual(
			0, World->GetComponentLiveTuningEntityOverrides().Num()));
		ASSERT_THAT(AreEqual(
			1, World->GetComponentLiveTuningAuthoredAbilityGrants().Num()));
		World->StopSimulation();
	}
	/** The override set is canonical state: RestoreSnapshot rejects any array
	 *  that is not strictly increasing by (Entity, PatchKey), so insertion must
	 *  hold that order regardless of the order edits arrive in. */
	TEST(ComponentLiveTuningEntityOverridesStayInCanonicalOrder,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		TArray<FSeinEntityHandle> Entities;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FSeinEntityHandle Entity = World->SpawnEntity(
					ASeinActor::StaticClass(), FFixedTransform(), Player);
				World->AddComponent(Entity, FSeinMovementComponent());
				Entities.Add(Entity);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0,
			TEXT("SeinARTS.ComponentLiveTuning.OverrideOrder"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		// Submit newest entity first, and each command's patches in reverse
		// reflection order, so arrival order never matches canonical order.
		for (int32 Index = Entities.Num() - 1; Index >= 0; --Index)
		{
			const FSeinEntityHandle Entity = Entities[Index];
			const FSeinMovementComponent* Before =
				World->GetComponent<FSeinMovementComponent>(Entity);
			ASSERT_THAT(IsNotNull(Before));
			FSeinMovementComponent After = *Before;
			After.TopSpeed = FFixedPoint::FromInt(700 + Index);
			After.TurnRate = FFixedPoint::FromInt(9 + Index);
			After.AvoidanceStrength = FFixedPoint::FromInt(2 + Index);

			FSeinComponentLiveTuningRequest Payload;
			Payload.Scope = ESeinComponentLiveTuningScope::Entity;
			Payload.TargetEntity = Entity;
			Payload.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
			Payload.Patches = MakeMovementPatches(
				*Before, After, ESeinComponentInstanceOverrideOperation::Set);
			ASSERT_THAT(AreEqual(3, Payload.Patches.Num()));
			Algo::Reverse(Payload.Patches);
			ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
				MakeLiveTuningCommand(Payload, Player), true)));
			FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(IsTrue(IsCanonicalOverrideOrder(
				World->GetComponentLiveTuningEntityOverrides())));
		}
		ASSERT_THAT(AreEqual(
			12, World->GetComponentLiveTuningEntityOverrides().Num()));

		// A reset-to-default in the middle of the set must drop exactly one
		// record and leave the remainder canonical.
		const FSeinEntityHandle ClearedEntity = Entities[1];
		const FSeinMovementComponent* ClearedBefore =
			World->GetComponent<FSeinMovementComponent>(ClearedEntity);
		ASSERT_THAT(IsNotNull(ClearedBefore));
		FSeinMovementComponent ClearedAfter = *ClearedBefore;
		ClearedAfter.TurnRate = FSeinMovementComponent().TurnRate;
		FSeinComponentLiveTuningRequest ClearPayload;
		ClearPayload.Scope = ESeinComponentLiveTuningScope::Entity;
		ClearPayload.TargetEntity = ClearedEntity;
		ClearPayload.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		ClearPayload.Patches = MakeMovementPatches(
			*ClearedBefore, ClearedAfter,
			ESeinComponentInstanceOverrideOperation::Clear);
		ASSERT_THAT(AreEqual(1, ClearPayload.Patches.Num()));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(ClearPayload, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(
			11, World->GetComponentLiveTuningEntityOverrides().Num()));
		ASSERT_THAT(IsTrue(IsCanonicalOverrideOrder(
			World->GetComponentLiveTuningEntityOverrides())));

		// RestoreSnapshot independently re-validates the same ordering contract.
		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		const int32 SnapshotStateHash = World->ComputeStateHash();
		ASSERT_THAT(AreEqual(
			11, Snapshot.ComponentLiveTuningEntityOverrides.Num()));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, Snapshot)));
		ASSERT_THAT(AreEqual(SnapshotStateHash, World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(IsCanonicalOverrideOrder(
			World->GetComponentLiveTuningEntityOverrides())));
		World->StopSimulation();
	}
	/** The wire body carries the editor-resolved per-class entries; the sim
	 *  resolves entities nearest-derived-first along the static class chain
	 *  and never infers inheritance from a class default. */
	TEST(ComponentLiveTuningDerivedClassEntriesRoundTripAndAreBounded,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FSeinMovementComponent Before;
		FSeinComponentLiveTuningRequest Request;
		Request.Scope = ESeinComponentLiveTuningScope::ActorClass;
		Request.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		Request.Patches.Add(MakeTopSpeedPatch(
			Before, FFixedPoint::FromInt(640),
			ESeinComponentInstanceOverrideOperation::None));
		FSeinComponentLiveTuningClassEntry& Inheriting =
			Request.DerivedClassEntries.AddDefaulted_GetRef();
		Inheriting.ActorClassPath = TEXT("/Game/Units/BP_Infantry.BP_Infantry_C");
		Inheriting.Patches = Request.Patches;
		FSeinComponentLiveTuningClassEntry& Pinned =
			Request.DerivedClassEntries.AddDefaulted_GetRef();
		Pinned.ActorClassPath = TEXT("/Game/Units/BP_Infantry_Elite.BP_Infantry_Elite_C");
		Pinned.Patches.Add(MakeTopSpeedPatch(
			Before, FFixedPoint::FromInt(720),
			ESeinComponentInstanceOverrideOperation::None));

		FSeinComponentLiveTuningCommandPayload Payload;
		FString Error;
		ASSERT_THAT(IsTrue(SeinEncodeComponentLiveTuningRequest(
			Request, Payload, Error)));
		FSeinComponentLiveTuningRequest Decoded;
		ASSERT_THAT(IsTrue(SeinDecodeComponentLiveTuningRequest(
			Payload, Decoded, Error)));
		ASSERT_THAT(IsTrue(Decoded.Scope == Request.Scope));
		ASSERT_THAT(AreEqual(Request.ActorClassPath, Decoded.ActorClassPath));
		ASSERT_THAT(AreEqual(1, Decoded.Patches.Num()));
		ASSERT_THAT(AreEqual(
			Request.Patches[0].ExportedValue, Decoded.Patches[0].ExportedValue));
		ASSERT_THAT(AreEqual(2, Decoded.DerivedClassEntries.Num()));
		ASSERT_THAT(AreEqual(
			Inheriting.ActorClassPath, Decoded.DerivedClassEntries[0].ActorClassPath));
		ASSERT_THAT(AreEqual(
			Inheriting.Patches[0].ExportedValue,
			Decoded.DerivedClassEntries[0].Patches[0].ExportedValue));
		ASSERT_THAT(AreEqual(
			Pinned.ActorClassPath, Decoded.DerivedClassEntries[1].ActorClassPath));
		ASSERT_THAT(AreEqual(
			Pinned.Patches[0].ExportedValue,
			Decoded.DerivedClassEntries[1].Patches[0].ExportedValue));

		// Legacy-shaped requests (no derived entries) still round-trip.
		Request.DerivedClassEntries.Reset();
		ASSERT_THAT(IsTrue(SeinEncodeComponentLiveTuningRequest(
			Request, Payload, Error)));
		ASSERT_THAT(IsTrue(SeinDecodeComponentLiveTuningRequest(
			Payload, Decoded, Error)));
		ASSERT_THAT(AreEqual(0, Decoded.DerivedClassEntries.Num()));

		// Entries are bounded exactly like patches, and each needs patches.
		FSeinComponentLiveTuningClassEntry Empty;
		Empty.ActorClassPath = TEXT("/Game/Units/BP_Empty.BP_Empty_C");
		Request.DerivedClassEntries.Add(Empty);
		ASSERT_THAT(IsFalse(SeinEncodeComponentLiveTuningRequest(
			Request, Payload, Error)));
		Request.DerivedClassEntries.Reset();
		for (int32 Index = 0; Index < 257; ++Index)
		{
			FSeinComponentLiveTuningClassEntry& Entry =
				Request.DerivedClassEntries.AddDefaulted_GetRef();
			Entry.ActorClassPath =
				FString::Printf(TEXT("/Game/Units/BP_%03d.BP_%03d_C"), Index, Index);
			Entry.Patches = Request.Patches;
		}
		ASSERT_THAT(IsFalse(SeinEncodeComponentLiveTuningRequest(
			Request, Payload, Error)));
	}

	/** A parent-class edit applies to the parent's entities, to a derived class
	 *  listed as inheriting, and to an unlisted derived class (one the editor
	 *  could not observe, which falls through to the nearest ancestor record),
	 *  while a derived class listed with a pin keeps its own value. Late spawns
	 *  resolve through the same chain, and malformed lists change nothing. */
	TEST(ComponentLiveTuningResolvesClassOverlaysNearestDerivedFirst,
		"SeinARTS.Unit.Entity.LiveTuning")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle BaseEntity;
		FSeinEntityHandle InheritingEntity;
		FSeinEntityHandle PinnedEntity;
		FSeinEntityHandle UnseenEntity;
		FSeinMovementComponent AuthoredMovement;
		AuthoredMovement.TopSpeed = FFixedPoint::FromInt(500);
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			BaseEntity = World->SpawnEntity(
				ASeinActor::StaticClass(), FFixedTransform(), Player);
			InheritingEntity = World->SpawnEntity(
				ASeinEffectReplayTestActor::StaticClass(), FFixedTransform(), Player);
			PinnedEntity = World->SpawnEntity(
				ASeinPlacementYawTestBuilding::StaticClass(), FFixedTransform(), Player);
			UnseenEntity = World->SpawnEntity(
				ASeinProductionCostTestActor::StaticClass(), FFixedTransform(), Player);
			for (const FSeinEntityHandle Entity :
				{BaseEntity, InheritingEntity, PinnedEntity, UnseenEntity})
			{
				World->AddComponent(Entity, AuthoredMovement);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState, FSeinMatchSettings(), 0,
			TEXT("SeinARTS.ComponentLiveTuning.ChainOverlay"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		const auto TopSpeedOf = [World](FSeinEntityHandle Entity)
		{
			const FSeinMovementComponent* Movement =
				World->GetComponent<FSeinMovementComponent>(Entity);
			return Movement ? Movement->TopSpeed : FFixedPoint::Zero;
		};

		FSeinComponentLiveTuningRequest Request;
		Request.Scope = ESeinComponentLiveTuningScope::ActorClass;
		Request.ActorClassPath = ASeinActor::StaticClass()->GetPathName();
		Request.Patches.Add(MakeTopSpeedPatch(
			AuthoredMovement, FFixedPoint::FromInt(800),
			ESeinComponentInstanceOverrideOperation::None));
		{
			FSeinComponentLiveTuningClassEntry Inheriting;
			Inheriting.ActorClassPath =
				ASeinEffectReplayTestActor::StaticClass()->GetPathName();
			Inheriting.Patches = Request.Patches;
			FSeinComponentLiveTuningClassEntry Pinned;
			Pinned.ActorClassPath =
				ASeinPlacementYawTestBuilding::StaticClass()->GetPathName();
			FSeinMovementComponent PinBaseline = AuthoredMovement;
			PinBaseline.TopSpeed = FFixedPoint::FromInt(1);
			Pinned.Patches.Add(MakeTopSpeedPatch(
				PinBaseline, FFixedPoint::FromInt(500),
				ESeinComponentInstanceOverrideOperation::None));
			Request.DerivedClassEntries = {Inheriting, Pinned};
			Request.DerivedClassEntries.Sort([](
				const FSeinComponentLiveTuningClassEntry& Left,
				const FSeinComponentLiveTuningClassEntry& Right)
			{
				return Left.ActorClassPath < Right.ActorClassPath;
			});
		}
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(Request, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(IsTrue(TopSpeedOf(BaseEntity) == FFixedPoint::FromInt(800)));
		ASSERT_THAT(IsTrue(TopSpeedOf(InheritingEntity) == FFixedPoint::FromInt(800)));
		ASSERT_THAT(IsTrue(TopSpeedOf(PinnedEntity) == FFixedPoint::FromInt(500)));
		ASSERT_THAT(IsTrue(TopSpeedOf(UnseenEntity) == FFixedPoint::FromInt(800)));
		// One exact-class record per listed class, canonical order.
		ASSERT_THAT(AreEqual(
			3, World->GetComponentLiveTuningClassDefaults().Num()));
		for (int32 Index = 1;
			Index < World->GetComponentLiveTuningClassDefaults().Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				World->GetComponentLiveTuningClassDefaults()[Index - 1].ActorClassPath
					< World->GetComponentLiveTuningClassDefaults()[Index].ActorClassPath));
		}

		// Late spawns resolve through the same chain: an unseen derived class
		// inherits the ancestor record, a pinned class keeps its pin.
		FSeinEntityHandle LateUnseen;
		FSeinEntityHandle LatePinned;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			LateUnseen = World->SpawnEntity(
				ASeinProductionCostTestActor::StaticClass(), FFixedTransform(), Player);
			World->AddComponent(LateUnseen, AuthoredMovement);
			LatePinned = World->SpawnEntity(
				ASeinPlacementYawTestBuilding::StaticClass(), FFixedTransform(), Player);
			World->AddComponent(LatePinned, AuthoredMovement);
		}
		// AddComponent after spawn bypasses the spawn-time overlay by design
		// (the payload is supplied explicitly); a subsequent class command
		// re-resolves every live entity through the chain.
		FSeinComponentLiveTuningRequest Second = Request;
		Second.Patches[0] = MakeTopSpeedPatch(
			AuthoredMovement, FFixedPoint::FromInt(850),
			ESeinComponentInstanceOverrideOperation::None);
		for (FSeinComponentLiveTuningClassEntry& Entry : Second.DerivedClassEntries)
		{
			if (Entry.ActorClassPath
				== ASeinEffectReplayTestActor::StaticClass()->GetPathName())
			{
				Entry.Patches = Second.Patches;
			}
		}
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(Second, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(TopSpeedOf(BaseEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(InheritingEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(PinnedEntity) == FFixedPoint::FromInt(500)));
		ASSERT_THAT(IsTrue(TopSpeedOf(UnseenEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(LateUnseen) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(LatePinned) == FFixedPoint::FromInt(500)));

		// A class-scoped pin from a class that loads mid-session retargets only
		// its own subtree, nearest-derived-first.
		FSeinComponentLiveTuningRequest LatePin;
		LatePin.Scope = ESeinComponentLiveTuningScope::ActorClass;
		LatePin.ActorClassPath =
			ASeinProductionCostTestActor::StaticClass()->GetPathName();
		LatePin.Patches.Add(MakeTopSpeedPatch(
			AuthoredMovement, FFixedPoint::FromInt(610),
			ESeinComponentInstanceOverrideOperation::None));
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(LatePin, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(IsTrue(TopSpeedOf(UnseenEntity) == FFixedPoint::FromInt(610)));
		ASSERT_THAT(IsTrue(TopSpeedOf(LateUnseen) == FFixedPoint::FromInt(610)));
		ASSERT_THAT(IsTrue(TopSpeedOf(BaseEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(InheritingEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(AreEqual(
			4, World->GetComponentLiveTuningClassDefaults().Num()));

		// Malformed derived lists (self-listed, unsorted) are rejected at
		// execution and change neither the record set nor any entity.
		FSeinComponentLiveTuningRequest SelfListed = Request;
		SelfListed.Patches[0] = MakeTopSpeedPatch(
			AuthoredMovement, FFixedPoint::FromInt(999),
			ESeinComponentInstanceOverrideOperation::None);
		{
			FSeinComponentLiveTuningClassEntry Self;
			Self.ActorClassPath = Request.ActorClassPath;
			Self.Patches = SelfListed.Patches;
			SelfListed.DerivedClassEntries.Add(Self);
			SelfListed.DerivedClassEntries.Sort([](
				const FSeinComponentLiveTuningClassEntry& Left,
				const FSeinComponentLiveTuningClassEntry& Right)
			{
				return Left.ActorClassPath < Right.ActorClassPath;
			});
		}
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(SelfListed, Player), true)));
		FSeinComponentLiveTuningRequest Unsorted = Request;
		Unsorted.Patches[0] = SelfListed.Patches[0];
		{
			FSeinComponentLiveTuningClassEntry Last;
			Last.ActorClassPath = TEXT("/Script/ZZZ_Last.ZZZ_Last_C");
			Last.Patches = Unsorted.Patches;
			Unsorted.DerivedClassEntries.Insert(Last, 0);
			ASSERT_THAT(IsTrue(
				Unsorted.DerivedClassEntries[0].ActorClassPath
					> Unsorted.DerivedClassEntries[1].ActorClassPath));
		}
		ASSERT_THAT(IsTrue(World->SubmitLocalCommandDraft(
			MakeLiveTuningCommand(Unsorted, Player), true)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(
			4, World->GetComponentLiveTuningClassDefaults().Num()));
		ASSERT_THAT(IsTrue(TopSpeedOf(BaseEntity) == FFixedPoint::FromInt(850)));
		ASSERT_THAT(IsTrue(TopSpeedOf(PinnedEntity) == FFixedPoint::FromInt(500)));

		// Snapshot round-trip of the chain-resolved record set.
		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		const int32 SnapshotStateHash = World->ComputeStateHash();
		ASSERT_THAT(AreEqual(4, Snapshot.ComponentLiveTuningClassDefaults.Num()));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, Snapshot)));
		ASSERT_THAT(AreEqual(SnapshotStateHash, World->ComputeStateHash()));
		World->StopSimulation();
	}
}
