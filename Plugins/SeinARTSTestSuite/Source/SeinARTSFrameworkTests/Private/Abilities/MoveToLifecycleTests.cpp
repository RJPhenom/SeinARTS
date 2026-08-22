#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Formations/SeinFormation.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Types/Entity.h"
#include "Misc/SecureHash.h"

bool USeinMoveToEscapeTestNavigation::bPassable = true;
bool USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = false;
int32 USeinMoveToEscapeTestNavigation::EscapeQueryCount = 0;
FFixedVector USeinMoveToEscapeTestNavigation::EscapeTarget =
	FFixedVector::ZeroVector;
FSeinEscapeQuery USeinMoveToEscapeTestNavigation::LastEscapeQuery;

void USeinMoveToEscapeTestNavigation::Reset()
{
	bPassable = true;
	bReturnEscapeTarget = false;
	EscapeQueryCount = 0;
	EscapeTarget = FFixedVector::ZeroVector;
	LastEscapeQuery = FSeinEscapeQuery();
}

bool USeinMoveToEscapeTestNavigation::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutError.Reset();
	FMD5 Hash;
	static constexpr ANSICHAR StableId[] =
		"seinarts.tests.navigation.move_to_escape/v1";
	Hash.Update(
		reinterpret_cast<const uint8*>(StableId),
		static_cast<uint32>(UE_ARRAY_COUNT(StableId) - 1));
	const uint8 Flags[] = {
		bPassable ? uint8{1} : uint8{0},
		bReturnEscapeTarget ? uint8{1} : uint8{0}
	};
	Hash.Update(Flags, UE_ARRAY_COUNT(Flags));
	auto AppendInt64 = [&Hash](int64 Value)
	{
		uint8 Bytes[8];
		const uint64 Bits = static_cast<uint64>(Value);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Bits >> (Index * 8));
		}
		Hash.Update(Bytes, UE_ARRAY_COUNT(Bytes));
	};
	AppendInt64(EscapeTarget.X.Value);
	AppendInt64(EscapeTarget.Y.Value);
	AppendInt64(EscapeTarget.Z.Value);

	uint8 Digest[16];
	Hash.Final(Digest);
	auto ReadUInt32 = [&Digest](int32 Offset)
	{
		return static_cast<uint32>(Digest[Offset])
			| (static_cast<uint32>(Digest[Offset + 1]) << 8)
			| (static_cast<uint32>(Digest[Offset + 2]) << 16)
			| (static_cast<uint32>(Digest[Offset + 3]) << 24);
	};
	OutDigest = FGuid(
		ReadUInt32(0),
		ReadUInt32(4),
		ReadUInt32(8),
		ReadUInt32(12));
	return true;
}

bool USeinMoveToEscapeTestNavigation::ComputeStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	OutClaim.StableImplementationId =
		TEXT("seinarts.tests.navigation.move_to_escape");
	OutClaim.BehaviorRevision = 1;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage = ESeinNavigationStateCoverage::Stateless;
	return true;
}

bool USeinMoveToEscapeTestNavigation::QueryEscapeTarget(
	const FSeinEscapeQuery& Query,
	FFixedVector& OutTarget) const
{
	++EscapeQueryCount;
	LastEscapeQuery = Query;
	if (!bReturnEscapeTarget)
	{
		return false;
	}
	OutTarget = EscapeTarget;
	return true;
}

bool USeinMoveToLifecycleTestMovement::bFinishOnTick = false;
int32 USeinMoveToLifecycleTestMovement::BeginCount = 0;
int32 USeinMoveToLifecycleTestMovement::TickCount = 0;
int32 USeinMoveToLifecycleTestMovement::EndCount = 0;
int32 USeinMoveToLifecycleTestMovement::PlanPathCallCount = 0;
int32 USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount = 0;
FFixedVector USeinMoveToLifecycleTestMovement::RepathWaypointMarker =
	FFixedVector::ZeroVector;
FFixedVector USeinMoveToLifecycleTestMovement::LastTickMiddleWaypoint =
	FFixedVector::ZeroVector;
TArray<ESeinPathResult>
	USeinMoveToLifecycleTestMovement::ScriptedPathResults;
TArray<int32> USeinMoveToLifecycleTestMovement::EmptyFoundCallIndices;
TArray<int32> USeinMoveToLifecycleTestMovement::FinishTickCallIndices;
bool USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = false;
bool USeinMoveToLifecycleTestMovement::bRepathPathsPartial = false;
bool USeinMoveToLifecycleTestMovement::bInitialPathPartial = false;
bool USeinMoveToLifecycleTestMovement::bInitialPathSkipsStart = false;
TFunction<void()> USeinMoveToLifecycleTestMovement::MoveEndCallback;

void USeinMoveToLifecycleTestMovement::Reset()
{
	bFinishOnTick = false;
	BeginCount = 0;
	TickCount = 0;
	EndCount = 0;
	PlanPathCallCount = 0;
	LastTickPathWaypointCount = 0;
	RepathWaypointMarker = FFixedVector::ZeroVector;
	LastTickMiddleWaypoint = FFixedVector::ZeroVector;
	ScriptedPathResults.Reset();
	EmptyFoundCallIndices.Reset();
	FinishTickCallIndices.Reset();
	bAdvanceInitialWaypointOnTick = false;
	bRepathPathsPartial = false;
	bInitialPathPartial = false;
	bInitialPathSkipsStart = false;
	MoveEndCallback = nullptr;
}

ESeinPathResult USeinMoveToLifecycleTestMovement::PlanPath(
	const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	const int32 CallIndex = PlanPathCallCount++;
	const ESeinPathResult Result = ScriptedPathResults.IsValidIndex(CallIndex)
		? ScriptedPathResults[CallIndex]
		: ESeinPathResult::Found;
	OutPath.Clear();
	if (Result != ESeinPathResult::Found)
	{
		return Result;
	}
	if (EmptyFoundCallIndices.Contains(CallIndex))
	{
		return ESeinPathResult::Found;
	}
	const FFixedVector Start = Ctx.Entity.Transform.GetLocation();
	if (CallIndex == 0 && bInitialPathSkipsStart)
	{
		OutPath.Waypoints.Add(FFixedVector(
			(Start.X + Ctx.Destination.X) / FFixedPoint::FromInt(2),
			(Start.Y + Ctx.Destination.Y) / FFixedPoint::FromInt(2),
			(Start.Z + Ctx.Destination.Z) / FFixedPoint::FromInt(2)));
	}
	else
	{
		OutPath.Waypoints.Add(Start);
	}
	if (CallIndex > 0 && RepathWaypointMarker != FFixedVector::ZeroVector)
	{
		OutPath.Waypoints.Add(RepathWaypointMarker);
	}
	OutPath.Waypoints.Add(Ctx.Destination);
	OutPath.bIsValid = true;
	OutPath.bIsPartial = (CallIndex == 0 && bInitialPathPartial)
		|| (CallIndex > 0 && bRepathPathsPartial);
	OutPath.DeriveSegmentsFromWaypoints();
	return ESeinPathResult::Found;
}

void USeinMoveToLifecycleTestMovement::OnMoveBegin(
	const FSeinMovementContext&)
{
	++BeginCount;
}

bool USeinMoveToLifecycleTestMovement::Tick(
	const FSeinMovementContext& Ctx)
{
	const int32 CallIndex = TickCount++;
	LastTickPathWaypointCount = Ctx.Path.Waypoints.Num();
	LastTickMiddleWaypoint = Ctx.Path.Waypoints.Num() > 2
		? Ctx.Path.Waypoints[1]
		: FFixedVector::ZeroVector;
	if (bAdvanceInitialWaypointOnTick
		&& Ctx.CurrentWaypointIndex == 0
		&& Ctx.Path.Waypoints.Num() > 1)
	{
		Ctx.CurrentWaypointIndex = 1;
	}
	return bFinishOnTick || FinishTickCallIndices.Contains(CallIndex);
}

void USeinMoveToLifecycleTestMovement::OnMoveEnd(FSeinEntity&)
{
	++EndCount;
	if (MoveEndCallback)
	{
		MoveEndCallback();
	}
}

void USeinMoveToLifecycleTestObserver::HandleCompleted(
	FSeinMoveToResult Result)
{
	++CompletedCount;
	bCompletedSawTerminalAction = Action
		&& Action->bCompleted
		&& !Action->bCancelled
		&& !Action->bFailed;
	if (Ability)
	{
		Ability->EndAbility();
	}
}

void USeinMoveToLifecycleTestObserver::HandleFailed(
	FSeinMoveToResult Result)
{
	++FailedCount;
	LastFailure = Result.FailureReason;
	bFailedSawTerminalAction = Action
		&& Action->bCompleted
		&& Action->bFailed
		&& !Action->bCancelled;
	if (Ability)
	{
		Ability->EndAbility();
	}
}

void USeinMoveToLifecycleTestObserver::HandleCancelled(
	FSeinMoveToResult Result)
{
	++CancelledCount;
	LastFailure = Result.FailureReason;
	if (bReenterCancellationOnCancelled && Manager && Ability)
	{
		Manager->CancelActionsForAbility(Ability);
	}
}

void USeinMoveToLifecycleTestObserver::HandlePathRecomputed(
	FSeinMoveToResult)
{
	++PathRecomputedCount;
	RepathEventOrder.Add(1);
	RecomputedObservedRepathElapsed = Action
		? UE::SeinARTSTests::FMoveToActionContinuationTestAccess::
			GetRepathElapsed(*Action)
		: FFixedPoint::MinValue;
}

void USeinMoveToLifecycleTestObserver::HandlePartialPath(
	FSeinMoveToResult)
{
	++PartialPathCount;
	PartialPathObservedBeginCount =
		USeinMoveToLifecycleTestMovement::BeginCount;
	RepathEventOrder.Add(2);
}

namespace
{
	struct FScopedDisabledNavigation
	{
		FScopedDisabledNavigation()
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedNavigationClass(Settings
				? Settings->NavigationClass
				: FSoftClassPath())
		{
			check(Settings);
			Settings->NavigationClass.Reset();
		}

		~FScopedDisabledNavigation()
		{
			Settings->NavigationClass = SavedNavigationClass;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath SavedNavigationClass;
	};

	struct FScopedEscapeNavigation
	{
		FScopedEscapeNavigation()
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedNavigationClass(Settings
				? Settings->NavigationClass
				: FSoftClassPath())
		{
			check(Settings);
			USeinMoveToEscapeTestNavigation::Reset();
			Settings->NavigationClass = FSoftClassPath(
				USeinMoveToEscapeTestNavigation::StaticClass()->GetPathName());
		}

		~FScopedEscapeNavigation()
		{
			Settings->NavigationClass = SavedNavigationClass;
			USeinMoveToEscapeTestNavigation::Reset();
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath SavedNavigationClass;
	};

	struct FScopedMoveToTestState
	{
		FScopedMoveToTestState()
		{
			USeinMoveToLifecycleTestMovement::Reset();
		}

		~FScopedMoveToTestState()
		{
			USeinMoveToLifecycleTestMovement::Reset();
		}
	};

	struct FMoveToLifecycleFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinLatentActionManager* Manager = nullptr;
		USeinMoveToLifecycleTestAbility* Ability = nullptr;
		USeinMoveToAction* Action = nullptr;
		USeinMoveToProxy* Proxy = nullptr;
		USeinMoveToLifecycleTestObserver* Observer = nullptr;
		FSeinEntityHandle Entity;
		FFixedVector Destination = FFixedVector(
			FFixedPoint::FromInt(100),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		int32 AbilityID = INDEX_NONE;

		bool Initialize(
			bool bFinishOnFirstTick,
			const FSeinNavigationComponent* NavigationComponent = nullptr)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				return false;
			}

			const bool bMaterialized = SeinTestMatchBootstrap::Materialize(
				*World, [&]()
				{
					Entity = World->SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					FSeinMovementComponent MovementComponent;
					MovementComponent.MovementClass = FSoftClassPath(
						USeinMoveToLifecycleTestMovement::StaticClass()->GetPathName());
					World->AddComponent(Entity, MovementComponent);
					if (NavigationComponent)
					{
						World->AddComponent(Entity, *NavigationComponent);
					}
					World->AddComponent(Entity, FSeinAbilityComponent());
					AbilityID = USeinAbilityBPFL::SeinGrantAbility(
						World, Entity,
						USeinMoveToLifecycleTestAbility::StaticClass());
				});
			if (!bMaterialized || !Entity.IsValid()
				|| !SeinTestMatchBootstrap::Start(*World))
			{
				return false;
			}

			Manager = World->LatentActionManager;
			if (!Manager)
			{
				return false;
			}

			Ability = Cast<USeinMoveToLifecycleTestAbility>(
				World->GetAbilityInstance(AbilityID));
			if (!Ability)
			{
				return false;
			}
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				if (!Ability->ActivateAbility(
					FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector))
				{
					return false;
				}
			}

			Proxy = NewObject<USeinMoveToProxy>(World);
			Action = NewObject<USeinMoveToAction>(Proxy);
			Observer = NewObject<USeinMoveToLifecycleTestObserver>(Proxy);
			if (!Proxy || !Action || !Observer)
			{
				return false;
			}

			Action->OwningAbility = Ability;
			Action->OwnerEntity = Entity;
			Action->Observer = Proxy;
			Action->Initialize(Destination);

			Observer->Ability = Ability;
			Observer->Action = Action;
			Observer->Manager = Manager;
			Proxy->OnCompleted.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleCompleted);
			Proxy->OnFailed.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleFailed);
			Proxy->OnCancelled.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleCancelled);
			Proxy->OnPathRecomputed.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandlePathRecomputed);
			Proxy->OnPartialPath.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandlePartialPath);

			USeinMoveToLifecycleTestMovement::bFinishOnTick =
				bFinishOnFirstTick;
			Manager->RegisterAction(Action);
			return true;
		}

		void Tick(FFixedPoint DeltaTime = FFixedPoint::One)
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Manager->TickAll(DeltaTime, *World);
		}

		void SetLocation(const FFixedVector& Location)
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			if (FSeinEntity* SimEntity = World->GetEntityMutable(Entity))
			{
				SimEntity->Transform.SetLocation(Location);
			}
		}

		FSeinEntityHandle SeedFrozenDestinationLifecycle()
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			const FSeinEntityHandle Broker = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			if (!Broker.IsValid()) return Broker;

			const FFixedPoint Radius =
				USeinFormation::GetFootprintRadius(World, Entity);
			FSeinFrozenDestination Previous;
			Previous.Member = Entity;
			Previous.WorldPosition = FFixedVector::ZeroVector;
			Previous.FootprintRadius = Radius;
			Previous.bReserveFootprint = true;
			Previous.SourceEntity = Broker;
			Previous.SourceIndex = 0;

			FSeinFrozenDestination Next = Previous;
			Next.WorldPosition = Destination;
			Next.SourceIndex = 1;

			FSeinBrokerQueuedOrder Order;
			Order.DestinationArtifact.Add(Next);
			Order.bIsExecuting = true;
			Order.LastDispatchTick = World->GetCurrentTick();
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members.Add(Entity);
			BrokerData.OrderQueue.Add(Order);
			BrokerData.SettledDestinationArtifact.Add(Previous);
			World->AddComponent(Broker, BrokerData);

			FSeinBrokerMembershipData Membership;
			Membership.CurrentBrokerHandle = Broker;
			World->AddComponent(Entity, Membership);
			return Broker;
		}
	};

	FSeinNavigationComponent MakeEscapeNavigationComponent()
	{
		FSeinNavigationComponent Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		Navigation.NavLayerMask = 0x04;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::FromInt(100);
		return Navigation;
	}

	FFixedPoint EscapeRecoveryStep()
	{
		return FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
	}

	FFixedVector EscapeRecoveryTarget()
	{
		return FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(1200),
			FFixedPoint::Zero);
	}
}

namespace UE::SeinARTSTests
{
	TEST(MoveToInitialThrottleRetriesBeforeMoveBegin,
		"SeinARTS.Sim.Movement.InitialPath")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Throttled,
			ESeinPathResult::Found
		};
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));

		Fixture.Tick();

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		const FSeinMovementComponent* Movement =
			Fixture.World->GetComponent<FSeinMovementComponent>(
				Fixture.Entity);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(Movement->bHasTarget));
		ASSERT_THAT(IsTrue(
			Movement->TargetLocation == Fixture.Destination));

		Fixture.Tick();

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToInitialNoNavigationFailsBeforeMoveBegin,
		"SeinARTS.Sim.Movement.InitialPath")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::NoNavigation
		};
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));

		Fixture.Tick();

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::NoNavigation),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
	}

	TEST(MoveToInitialEmptyFoundFailsAsPathNotFound,
		"SeinARTS.Sim.Movement.InitialPath")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::EmptyFoundCallIndices = {0};
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));

		Fixture.Tick();

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::PathNotFound),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
	}

	TEST(MoveToInitialPartialNotifiesBeforeMoveBegin,
		"SeinARTS.Sim.Movement.InitialPath")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bInitialPathPartial = true;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));

		Fixture.Tick();

		ASSERT_THAT(AreEqual(1, Fixture.Observer->PartialPathCount));
		ASSERT_THAT(AreEqual(
			0, Fixture.Observer->PartialPathObservedBeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(IsTrue(Fixture.Action->Path.bIsPartial));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToHeldButPassableDoesNotEscalate,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = true;
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToBlockedHoldExhaustsNoTargetAndStrands,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		ASSERT_THAT(AreEqual(
			EscapeRecoveryStep().Value,
			FMoveToActionContinuationTestAccess::GetHoldTime(
				*Fixture.Action).Value));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::HasStageOneFired(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			EscapeRecoveryStep().Value * 2,
			FMoveToActionContinuationTestAccess::GetNextEscalationAt(
				*Fixture.Action).Value));
		Fixture.Tick(EscapeRecoveryStep());
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			3, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(AreEqual(
			3,
			FMoveToActionContinuationTestAccess::GetEscapeAttempts(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::Stranded),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
	}

	TEST(MoveToEscapeQueryCarriesAgentProfileAndInstallsLeg,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget =
			EscapeRecoveryTarget();
		FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		const FGameplayTag BlockedTerrainTag =
			FGameplayTag::RequestGameplayTag(TEXT("Test"), false);
		ASSERT_THAT(IsTrue(BlockedTerrainTag.IsValid()));
		Navigation.BlockedTerrainTags.AddTag(BlockedTerrainTag);
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(AreEqual(2, Fixture.Action->Path.Waypoints.Num()));
		ASSERT_THAT(IsTrue(
			Fixture.Action->Path.Waypoints.Last()
				== EscapeRecoveryTarget()));
		ASSERT_THAT(IsTrue(
			USeinMoveToEscapeTestNavigation::LastEscapeQuery.From
				== FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			USeinMoveToEscapeTestNavigation::LastEscapeQuery.Requester
				== Fixture.Entity));
		ASSERT_THAT(AreEqual(
			4,
			static_cast<int32>(
				USeinMoveToEscapeTestNavigation::LastEscapeQuery.
					AgentNavLayerMask)));
		ASSERT_THAT(IsTrue(
			USeinMoveToEscapeTestNavigation::LastEscapeQuery.
				AgentFootprintRadius == FFixedPoint::FromInt(25)));
		ASSERT_THAT(IsTrue(
			USeinMoveToEscapeTestNavigation::LastEscapeQuery.
				BlockedTerrainTags.HasTagExact(BlockedTerrainTag)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsEscapeMode(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			0,
			FMoveToActionContinuationTestAccess::GetEscapeAttempts(
				*Fixture.Action)));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToRejectsEscapeTargetInsideEntryGate,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget = FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero);
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			1, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsEscapeMode(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			1,
			FMoveToActionContinuationTestAccess::GetEscapeAttempts(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			Fixture.Action->Path.Waypoints.Last()
				== Fixture.Destination));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToGenuineEscapeArrivalResolvesFreshOrderPath,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget =
			EscapeRecoveryTarget();
		USeinMoveToLifecycleTestMovement::FinishTickCallIndices = {2};
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.SetLocation(EscapeRecoveryTarget());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(IsTrue(Fixture.Action->Path.Waypoints.IsEmpty()));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));

		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(IsTrue(
			Fixture.Action->Path.Waypoints.Last()
				== Fixture.Destination));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToEscapeOvershootOutsideAcceptanceCountsAsFailedAttempt,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget =
			EscapeRecoveryTarget();
		USeinMoveToLifecycleTestMovement::FinishTickCallIndices = {2};
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(IsTrue(Fixture.Action->Path.Waypoints.IsEmpty()));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsEscapeMode(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			1,
			FMoveToActionContinuationTestAccess::GetEscapeAttempts(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToHeldEscapeLegExhaustsAndStrands,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget =
			EscapeRecoveryTarget();
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeRecoveryStep());
		const FFixedPoint EscapeHoldLimit =
			FFixedPoint::FromInt(3) / FFixedPoint::FromInt(5);
		Fixture.Tick(EscapeHoldLimit);
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeHoldLimit);
		Fixture.Tick(EscapeRecoveryStep());
		Fixture.Tick(EscapeHoldLimit);

		ASSERT_THAT(AreEqual(
			4, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(
			3, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::Stranded),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
	}

	TEST(MoveToEscapeEntryBudgetCapsOscillation,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		USeinMoveToEscapeTestNavigation::bReturnEscapeTarget = true;
		USeinMoveToEscapeTestNavigation::EscapeTarget =
			EscapeRecoveryTarget();
		const FSeinNavigationComponent Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::HasStageOneFired(
				*Fixture.Action)));
		FMoveToActionContinuationTestAccess::SetTotalEscapeEntries(
			*Fixture.Action, 5);
		Fixture.Tick(EscapeRecoveryStep());

		ASSERT_THAT(AreEqual(
			0, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::Stranded),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
	}

	TEST(MoveToCompletedCallbackCanEndAbilityWithoutCancellation,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true)));

		Fixture.Tick();

		ASSERT_THAT(IsTrue(Fixture.Observer->bCompletedSawTerminalAction));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CompletedCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsFalse(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));

		const FSeinMovementComponent* Movement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsFalse(Movement->bHasTarget));
		ASSERT_THAT(IsFalse(Movement->bArrivalImminent));
	}

	TEST(MoveToCancellationFinalizesMovementOnceUnderReentry,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));

		Fixture.Observer->bReenterCancellationOnCancelled = true;
		USeinMoveToLifecycleTestMovement::MoveEndCallback = [&]()
		{
			Fixture.Manager->CancelActionsForAbility(Fixture.Ability);
		};
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.Ability->CancelAbility();
		}
		Fixture.Manager->CleanupCompleted();

		ASSERT_THAT(IsTrue(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::Cancelled),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToFailureFinalizesMovementOnceAndRemainsFailure,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));

		USeinMoveToLifecycleTestMovement::MoveEndCallback = [&]()
		{
			Fixture.Manager->CancelActionsForAbility(Fixture.Ability);
		};
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->RemoveComponent<FSeinMovementComponent>(
				Fixture.Entity);
			Fixture.Manager->TickAll(FFixedPoint::FromInt(1), *Fixture.World);
		}

		ASSERT_THAT(IsTrue(Fixture.Observer->bFailedSawTerminalAction));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::NoMovementComponent),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(IsFalse(Fixture.Action->bCancelled));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToIntervalRepathCommitsBeforeMovementTick,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		const FFixedVector Marker(
			FFixedPoint::FromInt(40),
			FFixedPoint::FromInt(20),
			FFixedPoint::Zero);
		USeinMoveToLifecycleTestMovement::RepathWaypointMarker = Marker;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(20));

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount));
		ASSERT_THAT(IsTrue(
			USeinMoveToLifecycleTestMovement::LastTickMiddleWaypoint == Marker));
	}

	TEST(MoveToIntervalThrottleWaitsFullCadenceBeforeRetry,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(8);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint HalfInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToForcedIntervalRepathBypassesCadenceAndConsumesThrottle,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToForcedOffPathRepathBypassesCadenceAndDrift,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
	}

	TEST(MoveToForcedRepathRemainsPendingWithoutNavigation,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FScopedDisabledNavigation DisabledNavigation;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::Epsilon;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Epsilon));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToForcedRepathRemainsPendingWithoutNavigationSubsystem,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::FromInt(10);

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);

		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FMoveToActionContinuationTestAccess::
					TickRepathWithoutNavigationSubsystem(
						*Fixture.Action,
						FFixedPoint::Epsilon,
						*Fixture.World)));
		}

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Epsilon + FFixedPoint::Epsilon));
	}

	TEST(MoveToIntervalRepathFailsAtConfiguredLimit,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		Navigation.RepathFailureLimit = 2;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::NotFound,
			ESeinPathResult::NotFound
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint Interval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		Fixture.Tick(Interval);
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		Fixture.Tick(Interval);

		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::PathNotFound),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathRepathRequiresDriftAndMinimumCadence,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt - FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToOffPathImplicitOriginPrefixPreventsFalseDrift,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::bInitialPathSkipsStart = true;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(10));

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathThrottleWaitsMinimumCadenceBeforeRetry,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		Fixture.Tick(MinimumAttempt - FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToEmptyFoundAndNoNavigationCountAsRepathFailures,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		Navigation.RepathFailureLimit = 2;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found,
			ESeinPathResult::NoNavigation
		};
		USeinMoveToLifecycleTestMovement::EmptyFoundCallIndices = {1};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(Navigation.RepathInterval);
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));

		Fixture.Tick(Navigation.RepathInterval);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::PathNotFound),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToPartialRepathEmitsEventsInOrderWithoutRebegin,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};
		USeinMoveToLifecycleTestMovement::bRepathPathsPartial = true;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(Navigation.RepathInterval);

		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PartialPathCount));
		ASSERT_THAT(AreEqual(2, Fixture.Observer->RepathEventOrder.Num()));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->RepathEventOrder[0]));
		ASSERT_THAT(AreEqual(2, Fixture.Observer->RepathEventOrder[1]));
		ASSERT_THAT(IsTrue(
			Fixture.Observer->RecomputedObservedRepathElapsed
				== Navigation.RepathInterval));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathRepathFailsBeforeMovementAtLimit,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		Navigation.RepathFailureLimit = 1;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::NotFound
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToExactArrivalTransitionsFrozenDestinationClaim,
		"SeinARTS.Sim.Movement.FrozenDestination")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, &Navigation)));
		const FSeinEntityHandle Broker =
			Fixture.SeedFrozenDestinationLifecycle();
		ASSERT_THAT(IsTrue(Broker.IsValid()));

		Fixture.Tick();

		const FSeinCommandBrokerData* BrokerData =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(
			1, BrokerData->SettledDestinationArtifact.Num()));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact[0].WorldPosition
				== Fixture.Destination));
	}

	TEST(MoveToInitialPathFailureKeepsSettledFrozenDestination,
		"SeinARTS.Sim.Movement.FrozenDestination")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::NotFound
		};
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FSeinEntityHandle Broker =
			Fixture.SeedFrozenDestinationLifecycle();
		ASSERT_THAT(IsTrue(Broker.IsValid()));

		Fixture.Tick();

		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		const FSeinCommandBrokerData* BrokerData =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(AreEqual(
			1, BrokerData->SettledDestinationArtifact.Num()));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact[0].WorldPosition
				== FFixedVector::ZeroVector));
	}

	TEST(MoveToPartialCompletionDoesNotSettleFrozenDestination,
		"SeinARTS.Sim.Movement.FrozenDestination")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};
		USeinMoveToLifecycleTestMovement::bRepathPathsPartial = true;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, &Navigation)));
		const FSeinEntityHandle Broker =
			Fixture.SeedFrozenDestinationLifecycle();
		ASSERT_THAT(IsTrue(Broker.IsValid()));

		Fixture.Tick(Navigation.RepathInterval);

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsFalse(Fixture.Action->bFailed));
		const FSeinCommandBrokerData* BrokerData =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact.IsEmpty()));
	}
}
