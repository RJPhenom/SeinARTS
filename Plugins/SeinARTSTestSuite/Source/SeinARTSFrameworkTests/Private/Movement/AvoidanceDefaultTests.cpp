#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "HAL/IConsoleManager.h"
#include "Movement/SeinAvoidanceDefault.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const FFixedPoint TestRadius = FFixedPoint::FromInt(50);

	class FScopedParallelMode
	{
	public:
		FScopedParallelMode()
		{
			IConsoleManager& Console = IConsoleManager::Get();
			Parallel = Console.FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
			MinBatch = Console.FindConsoleVariable(
				TEXT("Sein.Sim.ParallelMinBatch"));
			if (Parallel && MinBatch)
			{
				SavedParallel = Parallel->GetInt();
				SavedMinBatch = MinBatch->GetInt();
			}
		}

		~FScopedParallelMode()
		{
			if (IsValid())
			{
				Parallel->SetWithCurrentPriority(SavedParallel);
				MinBatch->SetWithCurrentPriority(SavedMinBatch);
			}
		}

		bool IsValid() const
		{
			return Parallel && MinBatch;
		}

		bool Set(bool bParallel)
		{
			if (!IsValid())
			{
				return false;
			}
			Parallel->SetWithCurrentPriority(bParallel ? 1 : 0);
			MinBatch->SetWithCurrentPriority(1);
			return Parallel->GetInt() == (bParallel ? 1 : 0)
				&& MinBatch->GetInt() == 1;
		}

	private:
		IConsoleVariable* Parallel = nullptr;
		IConsoleVariable* MinBatch = nullptr;
		int32 SavedParallel = 0;
		int32 SavedMinBatch = 0;
	};

	struct FScopedIdleReseekSetting
	{
		explicit FScopedIdleReseekSetting(bool bEnabled)
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			bSaved = Settings->bIdleReseek;
			Settings->bIdleReseek = bEnabled;
		}

		~FScopedIdleReseekSetting()
		{
			Settings->bIdleReseek = bSaved;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		bool bSaved = false;
	};

	struct FAvoidanceFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinAvoidanceDefault* Avoidance = nullptr;
		TArray<FSeinEntityHandle> Movers;

		bool Initialize(TFunction<void(FAvoidanceFixture&)> Author)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				return false;
			}
			Avoidance = NewObject<USeinAvoidanceDefault>(World);
			if (!Avoidance)
			{
				return false;
			}
			return SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
				Author(*this);
			});
		}

		FSeinEntityHandle AddMover(
			const FFixedVector& Position,
			const FFixedVector& Velocity,
			const FFixedVector& Target,
			bool bHasTarget = true,
			FFixedPoint Strength = FFixedPoint::One)
		{
			const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
				FFixedTransform(Position), FSeinPlayerID(1));
			FSeinMovementComponent Movement;
			Movement.bHasTarget = bHasTarget;
			Movement.TargetLocation = Target;
			Movement.Velocity = Velocity;
			Movement.AvoidanceStrength = Strength;
			World->AddComponent(Handle, Movement);

			FSeinNavigationComponent Navigation;
			Navigation.FallbackFootprintRadius = TestRadius;
			World->AddComponent(Handle, Navigation);
			Movers.Add(Handle);
			return Handle;
		}

		bool Compute()
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCollisionSpatialHash* Hash =
				World->GetCollisionSpatialHashMutable();
			if (!Hash)
			{
				return false;
			}
			TArray<FSeinCollisionSpatialHash::FDynamicColliderInput> Colliders;
			for (const FSeinEntityHandle Handle : Movers)
			{
				const FSeinEntity* Entity = World->GetEntity(Handle);
				if (!Entity)
				{
					return false;
				}
				Colliders.Add({
					Handle, Entity->Transform.GetLocation(), TestRadius});
			}
			Hash->BuildDynamic(Colliders);
			Avoidance->ComputeAvoidance(*World);
			return true;
		}
	};

	void AuthorCrossingPair(
		FAvoidanceFixture& Fixture,
		FSeinEntityHandle& OutFirst,
		FSeinEntityHandle& OutSecond)
	{
		OutFirst = Fixture.AddMover(
			FFixedVector(
				FFixedPoint::FromInt(-60), FFixedPoint::Zero,
				FFixedPoint::Zero),
			FFixedVector(
				FFixedPoint::FromInt(100), FFixedPoint::Zero,
				FFixedPoint::Zero),
			FFixedVector(
				FFixedPoint::FromInt(1000), FFixedPoint::Zero,
				FFixedPoint::Zero));
		OutSecond = Fixture.AddMover(
			FFixedVector(
				FFixedPoint::FromInt(60), FFixedPoint::Zero,
				FFixedPoint::Zero),
			FFixedVector(
				FFixedPoint::FromInt(-100), FFixedPoint::Zero,
				FFixedPoint::Zero),
			FFixedVector(
				FFixedPoint::FromInt(-1000), FFixedPoint::Zero,
				FFixedPoint::Zero));
	}

	FSeinEntityHandle AddBroker(
		FAvoidanceFixture& Fixture,
		const TArray<FSeinEntityHandle>& Members,
		const FFixedVector& Centroid,
		FFixedPoint FormationRadius,
		bool bAvoidAsCohesiveBody,
		int64 CohesionGroupId = 0)
	{
		const FSeinEntityHandle Broker = Fixture.World->SpawnAbstractEntity(
			FFixedTransform(Centroid), FSeinPlayerID(1));
		FSeinCommandBrokerData BrokerData;
		BrokerData.Members = Members;
		BrokerData.Centroid = Centroid;
		BrokerData.FormationRadius = FormationRadius;
		BrokerData.bAvoidAsCohesiveBody = bAvoidAsCohesiveBody;
		BrokerData.bSelfCullOnEmpty = false;
		Fixture.World->AddComponent(Broker, BrokerData);
		for (const FSeinEntityHandle Member : Members)
		{
			FSeinBrokerMembershipData Membership;
			Membership.CurrentBrokerHandle = Broker;
			Membership.CohesionGroupId = CohesionGroupId;
			Fixture.World->AddComponent(Member, Membership);
		}
		return Broker;
	}
}

namespace UE::SeinARTSTests
{
	TEST(CrossingPairMatchesAcrossSerialAndParallelAvoidance,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FScopedParallelMode ParallelMode;
		ASSERT_THAT(IsTrue(ParallelMode.IsValid()));

		FAvoidanceFixture Serial;
		FAvoidanceFixture Parallel;
		FSeinEntityHandle SerialFirst;
		FSeinEntityHandle SerialSecond;
		FSeinEntityHandle ParallelFirst;
		FSeinEntityHandle ParallelSecond;
		ASSERT_THAT(IsTrue(Serial.Initialize([&](
			FAvoidanceFixture& Fixture)
		{
			AuthorCrossingPair(Fixture, SerialFirst, SerialSecond);
		})));
		ASSERT_THAT(IsTrue(Parallel.Initialize([&](
			FAvoidanceFixture& Fixture)
		{
			AuthorCrossingPair(Fixture, ParallelFirst, ParallelSecond);
		})));

		ASSERT_THAT(IsTrue(ParallelMode.Set(false)));
		ASSERT_THAT(IsTrue(Serial.Compute()));
		ASSERT_THAT(IsTrue(ParallelMode.Set(true)));
		ASSERT_THAT(IsTrue(Parallel.Compute()));

		const FSeinMovementComponent* SerialFirstMove =
			Serial.World->GetComponent<FSeinMovementComponent>(SerialFirst);
		const FSeinMovementComponent* SerialSecondMove =
			Serial.World->GetComponent<FSeinMovementComponent>(SerialSecond);
		const FSeinMovementComponent* ParallelFirstMove =
			Parallel.World->GetComponent<FSeinMovementComponent>(ParallelFirst);
		const FSeinMovementComponent* ParallelSecondMove =
			Parallel.World->GetComponent<FSeinMovementComponent>(ParallelSecond);
		ASSERT_THAT(IsNotNull(SerialFirstMove));
		ASSERT_THAT(IsNotNull(SerialSecondMove));
		ASSERT_THAT(IsNotNull(ParallelFirstMove));
		ASSERT_THAT(IsNotNull(ParallelSecondMove));

		ASSERT_THAT(IsTrue(
			SerialFirstMove->AvoidanceOutput.SteerDir.Y
				> FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SerialSecondMove->AvoidanceOutput.SteerDir.Y
				< FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SerialFirstMove->AvoidanceOutput.SpeedScale
				< FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			SerialFirstMove->AvoidanceOutput.SpeedScale
				> FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SerialSecondMove->AvoidanceOutput.SpeedScale
				< FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			SerialSecondMove->AvoidanceOutput.SpeedScale
				> FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SerialFirstMove->AvoidanceOutput.SteerDir
				== ParallelFirstMove->AvoidanceOutput.SteerDir));
		ASSERT_THAT(IsTrue(
			SerialSecondMove->AvoidanceOutput.SteerDir
				== ParallelSecondMove->AvoidanceOutput.SteerDir));
		ASSERT_THAT(IsTrue(
			SerialFirstMove->AvoidanceOutput.SpeedScale
				== ParallelFirstMove->AvoidanceOutput.SpeedScale));
		ASSERT_THAT(IsTrue(
			SerialSecondMove->AvoidanceOutput.SpeedScale
				== ParallelSecondMove->AvoidanceOutput.SpeedScale));
	}

	TEST(SameBrokerConvergingMembersSuppressMutualAvoidance,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			AuthorCrossingPair(Inner, First, Second);
			const FSeinEntityHandle Broker = Inner.World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID(1));
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {First, Second};
			BrokerData.bSelfCullOnEmpty = false;
			Inner.World->AddComponent(Broker, BrokerData);
			for (const FSeinEntityHandle Member : {First, Second})
			{
				FSeinBrokerMembershipData Membership;
				Membership.CurrentBrokerHandle = Broker;
				Inner.World->AddComponent(Member, Membership);
			}
			FSeinMovementComponent* SecondMove =
				Inner.World->GetComponentMutable<FSeinMovementComponent>(Second);
			check(SecondMove);
			SecondMove->TargetLocation = FFixedVector(
				FFixedPoint::FromInt(1000), FFixedPoint::Zero,
				FFixedPoint::Zero);
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* FirstMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(First);
		const FSeinMovementComponent* SecondMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(Second);
		ASSERT_THAT(IsNotNull(FirstMove));
		ASSERT_THAT(IsNotNull(SecondMove));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SpeedScale == FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SpeedScale == FFixedPoint::One));
	}

	TEST(LooseMoverTreatsCohesiveBrokerAsOneObstacle,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture SingleMemberFixture;
		FAvoidanceFixture DuplicateMemberFixture;
		FSeinEntityHandle SingleMover;
		FSeinEntityHandle DuplicateMover;
		auto Author = [](
			FAvoidanceFixture& Fixture,
			bool bDuplicateMember,
			FSeinEntityHandle& OutMover)
		{
			const FFixedVector Velocity(
				FFixedPoint::FromInt(100), FFixedPoint::Zero,
				FFixedPoint::Zero);
			const FFixedVector Target(
				FFixedPoint::FromInt(1000), FFixedPoint::Zero,
				FFixedPoint::Zero);
			OutMover = Fixture.AddMover(
				FFixedVector::ZeroVector, Velocity, Target);
			TArray<FSeinEntityHandle> Members;
			Members.Add(Fixture.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::FromInt(60),
					FFixedPoint::Zero),
				Velocity, Target));
			if (bDuplicateMember)
			{
				Members.Add(Fixture.AddMover(
					FFixedVector(
						FFixedPoint::FromInt(110), FFixedPoint::FromInt(80),
						FFixedPoint::Zero),
					Velocity, Target));
			}
			AddBroker(
				Fixture,
				Members,
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::FromInt(60),
					FFixedPoint::Zero),
				FFixedPoint::FromInt(100),
				true);
		};
		ASSERT_THAT(IsTrue(SingleMemberFixture.Initialize([&](
			FAvoidanceFixture& Fixture)
		{
			Author(Fixture, false, SingleMover);
		})));
		ASSERT_THAT(IsTrue(DuplicateMemberFixture.Initialize([&](
			FAvoidanceFixture& Fixture)
		{
			Author(Fixture, true, DuplicateMover);
		})));

		ASSERT_THAT(IsTrue(SingleMemberFixture.Compute()));
		ASSERT_THAT(IsTrue(DuplicateMemberFixture.Compute()));
		const FSeinMovementComponent* SingleMovement =
			SingleMemberFixture.World->GetComponent<FSeinMovementComponent>(
				SingleMover);
		const FSeinMovementComponent* DuplicateMovement =
			DuplicateMemberFixture.World->GetComponent<FSeinMovementComponent>(
				DuplicateMover);
		ASSERT_THAT(IsNotNull(SingleMovement));
		ASSERT_THAT(IsNotNull(DuplicateMovement));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SteerDir.Y < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SteerDir
				== DuplicateMovement->AvoidanceOutput.SteerDir));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SpeedScale
				== DuplicateMovement->AvoidanceOutput.SpeedScale));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SteerDir.Y.Value
				== -1196954227LL));
		ASSERT_THAT(IsTrue(
			SingleMovement->AvoidanceOutput.SpeedScale.Value
				== 4145348016LL));
	}

	TEST(CohesiveBrokersChooseAntisymmetricSides,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			AuthorCrossingPair(Inner, First, Second);
			AddBroker(
				Inner, {First},
				FFixedVector(
					FFixedPoint::FromInt(-60), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedPoint::FromInt(100), true);
			AddBroker(
				Inner, {Second},
				FFixedVector(
					FFixedPoint::FromInt(60), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedPoint::FromInt(100), true);
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* FirstMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(First);
		const FSeinMovementComponent* SecondMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(Second);
		ASSERT_THAT(IsNotNull(FirstMove));
		ASSERT_THAT(IsNotNull(SecondMove));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.Y > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.Y < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.Y.IsNearlyEqual(
				-SecondMove->AvoidanceOutput.SteerDir.Y)));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.Y.Value == 1190564918LL));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SpeedScale.Value == 4146146680LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.Y.Value == -1190564920LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SpeedScale.Value == 4146146680LL));
	}

	TEST(DistinctBrokersInSameCohesionGroupStillAvoidWhenCrossing,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			AuthorCrossingPair(Inner, First, Second);
			constexpr int64 CohesionGroupId = 9001;
			AddBroker(
				Inner, {First}, FFixedVector::ZeroVector,
				FFixedPoint::Zero, false, CohesionGroupId);
			AddBroker(
				Inner, {Second}, FFixedVector::ZeroVector,
				FFixedPoint::Zero, false, CohesionGroupId);
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* FirstMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(First);
		const FSeinMovementComponent* SecondMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(Second);
		ASSERT_THAT(IsNotNull(FirstMove));
		ASSERT_THAT(IsNotNull(SecondMove));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.Y > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.Y < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SteerDir.Y.Value == 1077177781LL));
		ASSERT_THAT(IsTrue(
			FirstMove->AvoidanceOutput.SpeedScale.Value == 4160320073LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SteerDir.Y.Value == -1077177783LL));
		ASSERT_THAT(IsTrue(
			SecondMove->AvoidanceOutput.SpeedScale.Value == 4160320073LL));
	}

	TEST(SameDirectionNeighborUsesGeometricSide,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle Mover;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			Mover = Inner.AddMover(
				FFixedVector::ZeroVector,
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero));
			Inner.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::FromInt(60),
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(50), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero));
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* Movement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Mover);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SteerDir.Y < FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SpeedScale < FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SpeedScale > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SteerDir.X.Value == 0LL));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SteerDir.Y.Value == -98796522LL));
		ASSERT_THAT(IsTrue(
			Movement->AvoidanceOutput.SpeedScale.Value == 4282617730LL));
	}

	TEST(IdleResolveDetoursAroundBlockerAndThreadsClearGap,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture DetourFixture;
		FSeinEntityHandle DetourMover;
		ASSERT_THAT(IsTrue(DetourFixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			DetourMover = Inner.AddMover(
				FFixedVector::ZeroVector,
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero));
			Inner.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector::ZeroVector,
				FFixedVector::ZeroVector,
				false);
		})));
		ASSERT_THAT(IsTrue(DetourFixture.Compute()));
		const FSeinMovementComponent* DetourMovement =
			DetourFixture.World->GetComponent<FSeinMovementComponent>(
				DetourMover);
		ASSERT_THAT(IsNotNull(DetourMovement));
		ASSERT_THAT(IsTrue(
			DetourMovement->AvoidanceOutput.SteerDir.SizeSquared()
				> FFixedPoint::Epsilon));

		FAvoidanceFixture GapFixture;
		FSeinEntityHandle GapMover;
		ASSERT_THAT(IsTrue(GapFixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			GapMover = Inner.AddMover(
				FFixedVector::ZeroVector,
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero));
			for (const int32 Side : {-1, 1})
			{
				Inner.AddMover(
					FFixedVector(
						FFixedPoint::FromInt(100),
						FFixedPoint::FromInt(120 * Side),
						FFixedPoint::Zero),
					FFixedVector::ZeroVector,
					FFixedVector::ZeroVector,
					false);
			}
		})));
		ASSERT_THAT(IsTrue(GapFixture.Compute()));
		const FSeinMovementComponent* GapMovement =
			GapFixture.World->GetComponent<FSeinMovementComponent>(GapMover);
		ASSERT_THAT(IsNotNull(GapMovement));
		ASSERT_THAT(IsTrue(
			GapMovement->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			GapMovement->AvoidanceOutput.SpeedScale == FFixedPoint::One));
	}

	TEST(BrokerCohesionBoostsLaggardAndHoldsLeader,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle Laggard;
		FSeinEntityHandle Leader;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			const FFixedVector SharedTarget(
				FFixedPoint::FromInt(1000), FFixedPoint::Zero,
				FFixedPoint::Zero);
			const FFixedVector SharedVelocity(
				FFixedPoint::FromInt(100), FFixedPoint::Zero,
				FFixedPoint::Zero);
			Laggard = Inner.AddMover(
				FFixedVector::ZeroVector, SharedVelocity, SharedTarget);
			Leader = Inner.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(200), FFixedPoint::Zero,
					FFixedPoint::Zero),
				SharedVelocity,
				SharedTarget);

			const FSeinEntityHandle Broker = Inner.World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID(1));
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members = {Laggard, Leader};
			BrokerData.bSelfCullOnEmpty = false;
			Inner.World->AddComponent(Broker, BrokerData);
			for (const FSeinEntityHandle Member : {Laggard, Leader})
			{
				FSeinBrokerMembershipData Membership;
				Membership.CurrentBrokerHandle = Broker;
				Inner.World->AddComponent(Member, Membership);
			}
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* LaggardMovement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Laggard);
		const FSeinMovementComponent* LeaderMovement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Leader);
		ASSERT_THAT(IsNotNull(LaggardMovement));
		ASSERT_THAT(IsNotNull(LeaderMovement));
		ASSERT_THAT(IsTrue(
			LaggardMovement->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			LeaderMovement->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			LaggardMovement->AvoidanceOutput.SpeedScale
				> FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			LeaderMovement->AvoidanceOutput.SpeedScale
				< FFixedPoint::One));
	}

	TEST(IdleDodgeActivatesForApproachAndReleasesExactly,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(true);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle Idle;
		FSeinEntityHandle Mover;
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			Idle = Inner.AddMover(
				FFixedVector(
					FFixedPoint::Zero, FFixedPoint::FromInt(20),
					FFixedPoint::Zero),
				FFixedVector::ZeroVector,
				FFixedVector::ZeroVector,
				false);
			Mover = Inner.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(-50), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero));
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* IdleMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(Idle);
		ASSERT_THAT(IsNotNull(IdleMove));
		ASSERT_THAT(IsTrue(
			IdleMove->AvoidanceOutput.SteerDir.Y > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			IdleMove->AvoidanceOutput.SpeedScale == FFixedPoint::One));

		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinMovementComponent* MoverMove =
				Fixture.World->GetComponentMutable<FSeinMovementComponent>(
					Mover);
			ASSERT_THAT(IsNotNull(MoverMove));
			MoverMove->bHasTarget = false;
		}
		ASSERT_THAT(IsTrue(Fixture.Compute()));
		IdleMove =
			Fixture.World->GetComponent<FSeinMovementComponent>(Idle);
		ASSERT_THAT(IsNotNull(IdleMove));
		ASSERT_THAT(IsTrue(
			IdleMove->AvoidanceOutput.SteerDir
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			IdleMove->AvoidanceOutput.SpeedScale == FFixedPoint::One));
	}

	TEST(OptedOutMoverPreservesAvoidanceOutput,
		"SeinARTS.Unit.Movement.Avoidance")
	{
		FScopedIdleReseekSetting IdleReseek(false);
		FAvoidanceFixture Fixture;
		FSeinEntityHandle Mover;
		const FSeinAvoidanceOutput SeededOutput{
			FFixedVector(
				FFixedPoint::FromInt(3), FFixedPoint::FromInt(4),
				FFixedPoint::Zero),
			FFixedPoint::One / FFixedPoint::Two};
		ASSERT_THAT(IsTrue(Fixture.Initialize([&](
			FAvoidanceFixture& Inner)
		{
			Mover = Inner.AddMover(
				FFixedVector(
					FFixedPoint::FromInt(200), FFixedPoint::FromInt(50),
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(100), FFixedPoint::Zero,
					FFixedPoint::Zero),
				FFixedVector(
					FFixedPoint::FromInt(1000), FFixedPoint::Zero,
					FFixedPoint::Zero),
				true,
				FFixedPoint::Zero);
			FSeinMovementComponent* Movement =
				Inner.World->GetComponentMutable<FSeinMovementComponent>(
					Mover);
			check(Movement);
			Movement->AvoidanceOutput = SeededOutput;
		})));

		ASSERT_THAT(IsTrue(Fixture.Compute()));
		const FSeinMovementComponent* Movement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Mover);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(Movement->AvoidanceOutput.SteerDir
			== SeededOutput.SteerDir));
		ASSERT_THAT(IsTrue(Movement->AvoidanceOutput.SpeedScale
			== SeededOutput.SpeedScale));
		ASSERT_THAT(IsTrue(Movement->PrevTickLocation
			== FFixedVector(
				FFixedPoint::FromInt(200), FFixedPoint::FromInt(50),
				FFixedPoint::Zero)));
	}
}
