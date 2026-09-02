#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinEntityControlPayload.h"
#include "Simulation/SeinTestSimContext.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinEntityControlBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace UE::SeinARTSTests
{
	TEST(EntityControlDefaultsToOwnerAndHonorsExactTickBoundedGrants,
		"SeinARTS.Unit.Authority")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Owner(1);
		const FSeinPlayerID Delegate(2);
		FSeinEntityHandle Entity;
		FSeinEntityControlGrantID RestrictedID;
		const auto AuthorState = [&]()
		{
			Entity = World->SpawnAbstractEntity(FFixedTransform(), Owner);
			RestrictedID = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, Entity, Delegate,
				{SeinARTSTags::Command_Type_ActivateAbility}, 3, 7);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.EntityControl.Bounds"))));
		ASSERT_THAT(IsTrue(Entity.IsValid()));
		ASSERT_THAT(IsTrue(RestrictedID.IsValid()));

		ASSERT_THAT(IsTrue(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Owner, Entity, SeinARTSTags::Command_Type_ActivateAbility, 0)));
		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_ActivateAbility, 0)));
		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, FSeinPlayerID::Neutral(), Entity,
			SeinARTSTags::Command_Type_ActivateAbility, 0)));

		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_ActivateAbility, 2)));
		ASSERT_THAT(IsTrue(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_ActivateAbility, 3)));
		ASSERT_THAT(IsTrue(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_ActivateAbility, 6)));
		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_ActivateAbility, 7)));
		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::CanPlayerControlEntityAtTick(
			*World, Delegate, Entity, SeinARTSTags::Command_Type_CancelAbility, 4)));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(USeinEntityControlBPFL::SeinRevokeEntityControl(
				World, RestrictedID)));
		}
		ASSERT_THAT(IsFalse(USeinEntityControlBPFL::IsEntityControlGrantActiveAtTick(
			*World, RestrictedID, 4)));
		World->StopSimulation();
	}

	TEST(EntityControlGrantIDsAndTagListsAreCanonicalAndNeverReused,
		"SeinARTS.Unit.Authority")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Owner(1);
		const FSeinPlayerID Delegate(2);
		FSeinEntityHandle A;
		FSeinEntityHandle B;
		FSeinEntityControlGrantID A1, A2, B1;
		const auto AuthorState = [&]()
		{
			A = World->SpawnAbstractEntity(FFixedTransform(), Owner);
			B = World->SpawnAbstractEntity(FFixedTransform(), Owner);
			A1 = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, A, Delegate,
				{SeinARTSTags::Command_Type_CancelAbility,
				 SeinARTSTags::Command_Type_ActivateAbility,
				 SeinARTSTags::Command_Type_CancelAbility,
				 FGameplayTag()}, 0, INDEX_NONE);
			A2 = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, A, Delegate, {}, 0, INDEX_NONE);
			B1 = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, B, Delegate, {}, 0, INDEX_NONE);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.EntityControl.Identity"))));

		ASSERT_THAT(IsTrue(A1.IsValid()));
		ASSERT_THAT(IsTrue(A2.IsValid()));
		ASSERT_THAT(IsTrue(B1.IsValid()));
		ASSERT_THAT(AreEqual(int64(1), A1.Serial));
		ASSERT_THAT(AreEqual(int64(2), A2.Serial));
		ASSERT_THAT(AreEqual(int64(1), B1.Serial));
		ASSERT_THAT(IsTrue(A1 != B1));

		const TArray<FSeinEntityControlGrant> Before =
			USeinEntityControlBPFL::SeinGetEntityControlGrants(World, A);
		ASSERT_THAT(AreEqual(2, Before.Num()));
		ASSERT_THAT(IsTrue(A1 == Before[0].GrantID));
		ASSERT_THAT(IsTrue(A2 == Before[1].GrantID));
		ASSERT_THAT(AreEqual(2, Before[0].AllowedCommandTypes.Num()));
		ASSERT_THAT(IsTrue(Before[0].AllowedCommandTypes[0].ToString()
			< Before[0].AllowedCommandTypes[1].ToString()));

		FSeinEntityControlGrantID A3;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(USeinEntityControlBPFL::SeinRevokeEntityControl(World, A1)));
			A3 = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, A, Delegate, {}, 0, INDEX_NONE);
		}
		ASSERT_THAT(AreEqual(int64(3), A3.Serial));
		ASSERT_THAT(IsTrue(A3 != A1));
		World->StopSimulation();
	}

	TEST(EntityControlStateSnapshotRestoresHashAndNextGrantIdentity,
		"SeinARTS.Unit.Authority")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Owner(1);
		const FSeinPlayerID Delegate(2);
		FSeinEntityHandle Entity;
		FSeinEntityControlGrantID FirstID;
		const auto AuthorState = [&]()
		{
			Entity = World->SpawnAbstractEntity(FFixedTransform(), Owner);
			FirstID = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, Entity, Delegate,
				{SeinARTSTags::Command_Type_ActivateAbility}, 0, INDEX_NONE);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.EntityControl.Snapshot"))));
		ASSERT_THAT(IsTrue(FirstID.IsValid()));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		const int32 CapturedHash = World->ComputeStateHash();

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(USeinEntityControlBPFL::SeinRevokeEntityControl(
				World, FirstID)));
			const FSeinEntityControlGrantID Mutation =
				USeinEntityControlBPFL::SeinGrantEntityControl(
					World, Entity, Delegate, {}, 0, 5);
			ASSERT_THAT(IsTrue(Mutation.IsValid()));
		}
		ASSERT_THAT(IsTrue(World->ComputeStateHash() != CapturedHash));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(*World, Snapshot)));
		ASSERT_THAT(AreEqual(CapturedHash, World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(USeinEntityControlBPFL::IsEntityControlGrantActiveAtTick(
			*World, FirstID, 0)));

		FSeinEntityControlGrantID NextID;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			NextID = USeinEntityControlBPFL::SeinGrantEntityControl(
				World, Entity, Delegate, {}, 0, INDEX_NONE);
		}
		ASSERT_THAT(AreEqual(int64(2), NextID.Serial));
		ASSERT_THAT(IsTrue(NextID != FirstID));
		World->StopSimulation();
	}
}
