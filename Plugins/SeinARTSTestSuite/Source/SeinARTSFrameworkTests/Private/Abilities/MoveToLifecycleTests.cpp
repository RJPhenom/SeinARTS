#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actions/SeinMoveToAction.h"
#include "Debug/SeinNavPathDebug.h"
#include "Debug/SeinDebugDrawCull.h"
#include "SeinMovementSubsystem.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasTypes.h"
#include "SceneView.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
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
FFixedPoint USeinMoveToLifecycleTestMovement::Deceleration =
	FFixedPoint::Zero;
int32 USeinMoveToLifecycleTestMovement::ArrivalMotionCount = 0;
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
	Deceleration = FFixedPoint::Zero;
	ArrivalMotionCount = 0;
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

FSeinMotion USeinMoveToLifecycleTestMovement::
ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover)
{
	++ArrivalMotionCount;
	return Super::ComputeArrivalMotion_Implementation(Mover);
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
	if (bEndAbilityOnPathRecomputed && Ability)
	{
		Ability->EndAbility();
	}
}

void USeinMoveToLifecycleTestObserver::HandlePartialPath(
	FSeinMoveToResult)
{
	++PartialPathCount;
	PartialPathObservedBeginCount =
		USeinMoveToLifecycleTestMovement::BeginCount;
	RepathEventOrder.Add(2);
}

void USeinMoveToLifecycleTestObserver::HandleWaypointReached(
	FSeinMoveToResult)
{
	++WaypointReachedCount;
	if (bEndAbilityOnWaypointReached && Ability)
	{
		Ability->EndAbility();
	}
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
			const FSeinNavigationPayload* NavigationComponent = nullptr)
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
					FSeinMovementPayload MovementComponent;
					MovementComponent.MovementClass = FSoftClassPath(
						USeinMoveToLifecycleTestMovement::StaticClass()->GetPathName());
					World->AddComponent(Entity, MovementComponent);
					if (NavigationComponent)
					{
						World->AddComponent(Entity, *NavigationComponent);
					}
					World->AddComponent(Entity, FSeinAbilityPayload());
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
			Proxy->OnWaypointReached.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleWaypointReached);

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

	FSeinNavigationPayload MakeEscapeNavigationComponent()
	{
		FSeinNavigationPayload Navigation;
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
	TEST(NavDebugCanvasDrawsCurrentViewWithoutRetainedLines, "SeinARTS.Integration.Navigation.Debug")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		// The lifecycle fixture normally wires a deliberately synthetic observer.
		// Use the production factory here so canonical capture sees the real graph.
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.Action->Observer.Reset();
			Fixture.Manager->CancelAllActions();
			Fixture.Proxy = USeinMoveToProxy::SeinMoveTo(Fixture.Ability, Fixture.Destination);
			Fixture.Proxy->Activate();
			Fixture.Action = FMoveToActionContinuationTestAccess::GetRunningAction(*Fixture.Proxy);
		}
		ASSERT_THAT(IsNotNull(Fixture.Action));
		Fixture.Tick();
		// The search chain deliberately bends away from the smoothed driven route.
		// A cell renderer must preserve these exact samples rather than rasterize the line.
		Fixture.Action->Path.DebugCellPath = {
			FFixedVector::ZeroVector,
			FFixedVector(FFixedPoint::Zero, FFixedPoint::FromInt(100), FFixedPoint::Zero),
			FFixedVector(FFixedPoint::FromInt(100), FFixedPoint::FromInt(100), FFixedPoint::Zero),
			Fixture.Destination};
		IConsoleVariable* Cells = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Nav.Show.RawCells"));
		ASSERT_THAT(IsNotNull(Cells));
		ASSERT_THAT(AreEqual(1, Cells->GetInt())); // Filled cells are part of the default view.
		const int32 SavedCells = Cells->GetInt();
		ON_SCOPE_EXIT { Cells->Set(SavedCells, ECVF_SetByCode); };
		UWorld* RenderWorld = Fixture.World->GetWorld();
		ASSERT_THAT(IsNotNull(RenderWorld->Scene));
		const int32 Flag = FEngineShowFlags::FindIndexByName(TEXT("SeinNavigation"));
		ASSERT_THAT(IsTrue(Flag != INDEX_NONE));
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(RenderWorld);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->InitAutoFormat(800, 600);
		Target->UpdateResourceImmediate(true);
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		ASSERT_THAT(IsNotNull(Resource));
		FIntPoint BentCellProbe, FinalCellProbe;
		auto Render = [&](bool bEnabled, TArray<FColor>& Pixels)
		{
			FEngineShowFlags Flags(ESFIM_Game);
			Flags.SetSingleFlag(Flag, bEnabled);
			FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Resource, RenderWorld->Scene, Flags)
				.SetTime(FGameTime::GetTimeSinceAppStart()));
			FSceneViewInitOptions Options;
			Options.ViewFamily = &Family;
			Options.SetViewRectangle(FIntRect(0, 0, 800, 600));
			Options.ViewOrigin = FVector(0, 0, 1000);
			Options.ViewRotationMatrix = FInverseRotationMatrix(FRotator(-90, 0, 0))
				* FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			Options.ProjectionMatrix = FReversedZOrthoMatrix(400, 300, 1.0 / 10000.0, 0);
			FSceneView View(Options);
			FCanvas Buffer(Resource, nullptr, RenderWorld, RenderWorld->GetFeatureLevel());
			Buffer.Clear(FLinearColor::Black);
			UCanvas* Canvas = NewObject<UCanvas>(RenderWorld);
			Canvas->Init(800, 600, &View, &Buffer);
			Canvas->Update();
			auto InteriorProbe = [&](const FVector& WorldPoint)
			{
				const FVector P = Canvas->Project(WorldPoint);
				// Offset from center markers and route lines, within the filled cell.
				return FIntPoint(FMath::RoundToInt(P.X) + 12, FMath::RoundToInt(P.Y) + 12);
			};
			BentCellProbe = InteriorProbe(FVector(0, 100, 15));
			FinalCellProbe = InteriorProbe(FVector(100, 0, 15));
			UDebugDrawService::Draw(Flags, Canvas);
			Buffer.Flush_GameThread();
			FlushRenderingCommands();
			return Resource->ReadPixels(Pixels);
		};
		auto RoutePixels = [](const TArray<FColor>& Pixels)
		{
			int32 Count = 0;
			// Excludes legend: count only the actual central world-path geometry.
			for (int32 Y = 200; Y < 450; ++Y)
				for (int32 X = 200; X < 650; ++X)
				{
					const FColor& C = Pixels[Y * 800 + X];
					if (C.R > 20 || C.G > 20 || C.B > 20) ++Count;
				}
			return Count;
		};
		TArray<FColor> Off, On, OffAgain, LinesOnly, WithoutHistory;
		FGuid RootBefore, RootAfter;
		FString RootError;
		const bool bRootCaptured = Fixture.World->ComputeCanonicalStateRoot(RootBefore, RootError);
		if (!bRootCaptured) UE_LOG(LogTemp, Error, TEXT("Nav debug fixture canonical root: %s"), *RootError);
		ASSERT_THAT(IsTrue(bRootCaptured));
		ASSERT_THAT(IsTrue(Render(false, Off)));
		ASSERT_THAT(IsTrue(Render(true, On)));
		ASSERT_THAT(IsTrue(Render(false, OffAgain)));
		Cells->Set(0, ECVF_SetByCode);
		ASSERT_THAT(IsTrue(Render(true, LinesOnly)));
		Cells->Set(1, ECVF_SetByCode);
		const TArray<FFixedVector> SavedHistory = Fixture.Action->Path.DebugCellPath;
		Fixture.Action->Path.DebugCellPath.Reset();
		ASSERT_THAT(IsTrue(Render(true, WithoutHistory)));
		Fixture.Action->Path.DebugCellPath = SavedHistory;
		ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(RootAfter, RootError)));
		ASSERT_THAT(AreEqual(RootBefore, RootAfter));
		ASSERT_THAT(AreEqual(0, RoutePixels(Off)));
		ASSERT_THAT(IsTrue(RoutePixels(On) > 10));
		// Filled interiors, not outlines or the old gray point markers.
		ASSERT_THAT(IsTrue(RoutePixels(On) > RoutePixels(LinesOnly) + 4000));
		ASSERT_THAT(AreEqual(RoutePixels(LinesOnly), RoutePixels(WithoutHistory)));
		const int32 BentIndex = BentCellProbe.Y * 800 + BentCellProbe.X;
		const int32 FinalIndex = FinalCellProbe.Y * 800 + FinalCellProbe.X;
		ASSERT_THAT(IsTrue(On.IsValidIndex(BentIndex) && On.IsValidIndex(FinalIndex)));
		ASSERT_THAT(IsTrue(On[BentIndex].R > 150 && On[BentIndex].G > 100 && On[BentIndex].B < 80));
		ASSERT_THAT(IsTrue(LinesOnly[BentIndex].R < 20 && LinesOnly[BentIndex].G < 20 && LinesOnly[BentIndex].B < 20));
		ASSERT_THAT(IsTrue(On[FinalIndex].B > 100 && On[FinalIndex].R < 100));
		ASSERT_THAT(AreEqual(0, RoutePixels(OffAgain)));
		TArray<uint8> PNG;
		FImageUtils::CompressImageArray(800, 600, On, PNG);
		const FString Preview = FPaths::ProjectSavedDir() / TEXT("Automation/NavDebugCanvas.png");
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(PNG, *Preview)));
	}

	TEST(SteeringCanvasShowsIdleMotionAndOwnsItsViewBudget, "SeinARTS.Integration.Steering.Debug")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		FSeinNavigationPayload Nav;
		Nav.FallbackFootprintRadius = FFixedPoint::FromInt(20);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Nav)));
		Fixture.Tick();
		UWorld* World = Fixture.World->GetWorld();
		USeinMovement* Driver = World->GetSubsystem<USeinMovementSubsystem>()->FindMovementInstance(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Driver));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			auto* Move = Fixture.World->GetComponentMutable<FSeinMovementPayload>(Fixture.Entity);
			Move->bHasTarget = false; // Idle coasting / sidestepping must remain visible.
			Move->Velocity = FFixedVector(FFixedPoint::FromInt(120), FFixedPoint::Zero, FFixedPoint::Zero);
			Move->AvoidanceOutput.SteerDir = FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero);
		}
		Driver->CaptureSteeringDebugMotion(Fixture.World->GetCurrentTick(),
			FFixedVector(FFixedPoint::FromInt(80), FFixedPoint::Zero, FFixedPoint::Zero), true);
		auto* Settings = GetMutableDefault<USeinARTSCoreSettings>();
		const int32 SavedCap = Settings->DebugDrawMaxEntities;
		const bool SavedLegends = Settings->bShowDebugLegends;
		Settings->DebugDrawMaxEntities = 1;
		Settings->bShowDebugLegends = true;
		ON_SCOPE_EXIT { Settings->DebugDrawMaxEntities = SavedCap; Settings->bShowDebugLegends = SavedLegends; };
		const int32 Flag = FEngineShowFlags::FindIndexByName(TEXT("SeinSteering"));
		ASSERT_THAT(IsTrue(Flag != INDEX_NONE));
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(World);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->InitAutoFormat(800, 600);
		Target->UpdateResourceImmediate(true);
		auto* Resource = Target->GameThread_GetRenderTargetResource();
		auto Render = [&](bool bEnabled, TArray<FColor>& Pixels, bool bAllPanels = false)
		{
			FEngineShowFlags Flags(ESFIM_Game);
			Flags.SetSingleFlag(Flag, bEnabled);
			for (const TCHAR* Name : { TEXT("SeinNavigation"), TEXT("SeinExtents"), TEXT("FogOfWar") })
			{
				const int32 PanelFlag = FEngineShowFlags::FindIndexByName(Name);
				if (PanelFlag != INDEX_NONE) Flags.SetSingleFlag(PanelFlag, bAllPanels);
			}
			FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Resource, World->Scene, Flags)
				.SetTime(FGameTime::GetTimeSinceAppStart()));
			FSceneViewInitOptions Options;
			Options.ViewFamily = &Family;
			Options.SetViewRectangle(FIntRect(0, 0, 800, 600));
			Options.ViewOrigin = FVector(0, 0, 1000);
			Options.ViewRotationMatrix = FInverseRotationMatrix(FRotator(-90, 0, 0))
				* FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			Options.ProjectionMatrix = FReversedZOrthoMatrix(400, 300, 1.0 / 10000.0, 0);
			FSceneView View(Options);
			FCanvas Buffer(Resource, nullptr, World, World->GetFeatureLevel());
			Buffer.Clear(FLinearColor::Black);
			UCanvas* Canvas = NewObject<UCanvas>(World);
			Canvas->Init(800, 600, &View, &Buffer);
			Canvas->Update();
			UDebugDrawService::Draw(Flags, Canvas);
			Buffer.Flush_GameThread();
			FlushRenderingCommands();
			return Resource->ReadPixels(Pixels);
		};
		auto Colored = [](const TArray<FColor>& Pixels, bool bRed)
		{
			int32 Count = 0;
			for (int32 Y = 150; Y < 450; ++Y)
				for (int32 X = 200; X < 650; ++X)
				{
					const FColor& C = Pixels[Y * 800 + X];
					if (bRed ? C.R > 150 && C.G < 100 && C.B < 100 : C.G > 150 && C.R < 100) ++Count;
				}
			return Count;
		};
		TArray<FColor> On, Off, Again;
		ASSERT_THAT(IsTrue(Render(true, On)));
		ASSERT_THAT(IsTrue(Colored(On, true) > 10));
		ASSERT_THAT(IsTrue(Colored(On, false) > 10));
		ASSERT_THAT(IsTrue(Render(false, Off)));
		ASSERT_THAT(AreEqual(0, Colored(Off, true) + Colored(Off, false)));
		// Exhaust the old Extents/ticker allowance. This view still gets its own budget.
		while (UE::SeinARTSMovement::DebugDraw::TryReserveBudget()) {}
		ASSERT_THAT(IsTrue(Render(true, Again)));
		ASSERT_THAT(AreEqual(Colored(On, true), Colored(Again, true)));
		ASSERT_THAT(AreEqual(Colored(On, false), Colored(Again, false)));
		Settings->bShowDebugLegends = false;
		TArray<FColor> WithoutLegend;
		ASSERT_THAT(IsTrue(Render(true, WithoutLegend)));
		ASSERT_THAT(AreEqual(Colored(On, true), Colored(WithoutLegend, true)));
		ASSERT_THAT(AreEqual(Colored(On, false), Colored(WithoutLegend, false)));
		TArray<FColor> AllHidden, AllVisible;
		ASSERT_THAT(IsTrue(Render(true, AllHidden, true)));
		Settings->bShowDebugLegends = true;
		ASSERT_THAT(IsTrue(Render(true, AllVisible, true)));
		// Each of the four actual view callbacks must contribute a panel, and
		// the shared setting must remove it without suppressing the geometry.
		const FIntPoint Bands[] = { {16, 128}, {136, 248}, {256, 336}, {344, 472} };
		for (const FIntPoint& Band : Bands)
		{
			int32 ChangedPixels = 0;
			for (int32 Y = Band.X; Y < Band.Y; ++Y)
				for (int32 X = 112; X < 784; ++X)
					if (AllVisible[Y * 800 + X] != AllHidden[Y * 800 + X]) ++ChangedPixels;
			ASSERT_THAT(IsTrue(ChangedPixels > 100));
		}
		TArray<uint8> LegendPNG;
		FImageUtils::CompressImageArray(800, 600, AllVisible, LegendPNG);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(LegendPNG, *(FPaths::ProjectSavedDir() / TEXT("Automation/DebugLegendPanels.png")))));
		TArray<uint8> PNG;
		FImageUtils::CompressImageArray(800, 600, On, PNG);
		ASSERT_THAT(IsTrue(FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("Automation/SteeringDebugCanvas.png")))));
	}

	TEST(SteeringDecisionClearsAtDispatchAndCancel, "SeinARTS.Unit.Steering.Debug")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		Fixture.Tick();
		USeinMovement* Driver = Fixture.World->GetWorld()->GetSubsystem<USeinMovementSubsystem>()->FindMovementInstance(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Driver));
		Driver->CaptureSteeringDebugTarget(Fixture.World->GetCurrentTick(), Fixture.Destination);
		Fixture.Tick(); // This test driver has no target diagnostics to replace it.
		ASSERT_THAT(IsFalse(Driver->GetSteeringDebugSample().bHasTarget));
		Driver->CaptureSteeringDebugTarget(Fixture.World->GetCurrentTick(), Fixture.Destination);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.Action->OnCancel();
		}
		ASSERT_THAT(IsFalse(Driver->GetSteeringDebugSample().bHasTarget));
	}

	TEST(NavDebugUsesOwningWorldManager, "SeinARTS.Unit.Navigation.Debug")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture First;
		FMoveToLifecycleFixture Second;
		ASSERT_THAT(IsTrue(First.Initialize(false)));
		ASSERT_THAT(IsTrue(Second.Initialize(false)));
		ASSERT_THAT(AreEqual(First.Entity, Second.Entity));
		TArray<const USeinMoveToAction*> Moves;
		UE::SeinARTSMovement::NavDebug::CollectActions(*First.World, Moves);
		ASSERT_THAT(AreEqual(1, Moves.Num()));
		ASSERT_THAT(IsTrue(Moves[0] == First.Action));
		UE::SeinARTSMovement::NavDebug::CollectActions(*Second.World, Moves);
		ASSERT_THAT(AreEqual(1, Moves.Num()));
		ASSERT_THAT(IsTrue(Moves[0] == Second.Action));
		// A valid handle in the wrong world is not proof of ownership.
		First.Action->OwningAbility = Second.Ability;
		UE::SeinARTSMovement::NavDebug::CollectActions(*First.World, Moves);
		First.Action->OwningAbility = First.Ability;
		ASSERT_THAT(IsTrue(Moves.IsEmpty()));
	}

	TEST(NavDebugOmitsTerminalAndUnmanagedActions, "SeinARTS.Unit.Navigation.Debug")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		USeinMoveToAction* Stray = NewObject<USeinMoveToAction>(Fixture.World);
		Stray->OwningAbility = Fixture.Ability;
		Stray->OwnerEntity = Fixture.Entity;
		TArray<const USeinMoveToAction*> Moves;
		UE::SeinARTSMovement::NavDebug::CollectActions(*Fixture.World, Moves);
		ASSERT_THAT(AreEqual(1, Moves.Num()));
		Fixture.Action->bCancelled = true;
		UE::SeinARTSMovement::NavDebug::CollectActions(*Fixture.World, Moves);
		ASSERT_THAT(IsTrue(Moves.IsEmpty()));
		Fixture.Action->bCancelled = false;
		Fixture.Action->bCompleted = true;
		UE::SeinARTSMovement::NavDebug::CollectActions(*Fixture.World, Moves);
		ASSERT_THAT(IsTrue(Moves.IsEmpty()));
	}

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
		const FSeinMovementPayload* Movement =
			Fixture.World->GetComponent<FSeinMovementPayload>(
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

	TEST(MoveToArrivalImminentTracksBrakeZoneAndClearsOnCompletion,
		"SeinARTS.Sim.Movement.ArrivalProgress")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToLifecycleTestMovement::Deceleration =
			FFixedPoint::FromInt(100);
		FSeinNavigationPayload Navigation;
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(4));

		const FSeinMovementPayload* Movement =
			Fixture.World->GetComponent<FSeinMovementPayload>(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(Movement->bArrivalImminent));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));

		USeinMoveToLifecycleTestMovement::bFinishOnTick = true;
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(4));

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsFalse(Movement->bArrivalImminent));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CompletedCount));
	}

	TEST(MoveToArrivalImminentClearsOutsideBrakeZoneAndForZeroDeceleration,
		"SeinARTS.Sim.Movement.ArrivalProgress")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToLifecycleTestMovement::Deceleration =
			FFixedPoint::FromInt(100);
		FSeinNavigationPayload Navigation;
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint TenthSecond =
			FFixedPoint::One / FFixedPoint::FromInt(10);

		Fixture.Tick(TenthSecond);
		const FSeinMovementPayload* Movement =
			Fixture.World->GetComponent<FSeinMovementPayload>(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(Movement->bArrivalImminent));

		Fixture.SetLocation(FFixedVector(
			FFixedPoint::FromInt(-2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero));
		Fixture.Tick(TenthSecond);
		ASSERT_THAT(IsFalse(Movement->bArrivalImminent));

		USeinMoveToLifecycleTestMovement::Deceleration = FFixedPoint::Zero;
		Fixture.SetLocation(FFixedVector::ZeroVector);
		Fixture.Tick(TenthSecond);
		ASSERT_THAT(IsFalse(Movement->bArrivalImminent));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
	}

	TEST(MoveToNearGoalStallSettlesAtExactThresholdThroughArrivalPolicy,
		"SeinARTS.Sim.Movement.ArrivalProgress")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = false;
		FSeinNavigationPayload Navigation = MakeEscapeNavigationComponent();
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint QuarterSecond =
			FFixedPoint::One / FFixedPoint::FromInt(4);

		Fixture.Tick(QuarterSecond);
		Fixture.Tick(QuarterSecond);
		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::ArrivalMotionCount));

		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsFalse(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::ArrivalMotionCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CompletedCount));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToEscapeTestNavigation::EscapeQueryCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
	}

	TEST(MoveToMeaningfulNearGoalProgressRearmsStallClock,
		"SeinARTS.Sim.Movement.ArrivalProgress")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		FSeinNavigationPayload Navigation;
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint QuarterSecond =
			FFixedPoint::One / FFixedPoint::FromInt(4);

		Fixture.Tick(QuarterSecond);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::FromInt(20),
			FFixedPoint::Zero,
			FFixedPoint::Zero));
		Fixture.Tick(QuarterSecond);
		Fixture.Tick(QuarterSecond);
		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::ArrivalMotionCount));

		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::ArrivalMotionCount));
	}

	TEST(MoveToSubTenCentimeterClosingDoesNotRearmStallClock,
		"SeinARTS.Sim.Movement.ArrivalProgress")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		FSeinNavigationPayload Navigation;
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint QuarterSecond =
			FFixedPoint::One / FFixedPoint::FromInt(4);

		Fixture.Tick(QuarterSecond);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::FromInt(9),
			FFixedPoint::Zero,
			FFixedPoint::Zero));
		Fixture.Tick(QuarterSecond);
		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		Fixture.Tick(QuarterSecond);

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::ArrivalMotionCount));
	}

	TEST(MoveToHeldButPassableDoesNotEscalate,
		"SeinARTS.Sim.Movement.EscapeRecovery")
	{
		FScopedMoveToTestState Reset;
		FScopedEscapeNavigation ScopedNavigation;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		USeinMoveToEscapeTestNavigation::bPassable = true;
		const FSeinNavigationPayload Navigation =
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
		const FSeinNavigationPayload Navigation =
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
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveStuckPhase::HoldingRepathed),
			static_cast<int32>(Fixture.Action->GetStuckPhase())));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			1,
			FMoveToActionContinuationTestAccess::GetHoldBoundariesFired(
				*Fixture.Action)));
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
		FSeinNavigationPayload Navigation =
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
		// The order path is discarded at escape entry; the harness drives the
		// two-point leg from the install position to the nav's target.
		ASSERT_THAT(IsTrue(Fixture.Action->Path.Waypoints.IsEmpty()));
		FSeinPath DrivenScratch;
		const FSeinPath& Driven =
			Fixture.Action->GetDrivenPath(DrivenScratch);
		ASSERT_THAT(IsTrue(Driven.bIsValid));
		ASSERT_THAT(AreEqual(2, Driven.Waypoints.Num()));
		ASSERT_THAT(AreEqual(1, Driven.Segments.Num()));
		ASSERT_THAT(IsTrue(
			Driven.Waypoints[0] == FFixedVector::ZeroVector));
		ASSERT_THAT(IsTrue(
			Driven.Waypoints.Last() == EscapeRecoveryTarget()));
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
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveStuckPhase::Escaping),
			static_cast<int32>(Fixture.Action->GetStuckPhase())));
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
		const FSeinNavigationPayload Navigation =
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
		// A no-answer keeps the episode open at stage 2 (no leg installed).
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveStuckPhase::HoldingRepathed),
			static_cast<int32>(Fixture.Action->GetStuckPhase())));
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
		const FSeinNavigationPayload Navigation =
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
		const FSeinNavigationPayload Navigation =
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
		// A failed leg returns to stage 2 with the hold clock preserved.
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveStuckPhase::HoldingRepathed),
			static_cast<int32>(Fixture.Action->GetStuckPhase())));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetHoldTime(
				*Fixture.Action) > FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(
			0,
			FMoveToActionContinuationTestAccess::GetHoldBoundariesFired(
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
		const FSeinNavigationPayload Navigation =
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
		const FSeinNavigationPayload Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));

		Fixture.Tick(EscapeRecoveryStep());
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveStuckPhase::HoldingRepathed),
			static_cast<int32>(Fixture.Action->GetStuckPhase())));
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

	TEST(MoveToPathRecomputedCallbackCanEndAbilityMidTick,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		// A Blueprint that ends the ability from the path-recomputed callback
		// cancels this action INSIDE its own tick, releasing the movement
		// instance before the movement stage would dereference it.
		FScopedMoveToTestState Reset;
		FSeinNavigationPayload Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Observer->bEndAbilityOnPathRecomputed = true;
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(20));

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		// The tick ended at the repath stage: no movement tick ran on the
		// released instance, and the move ended exactly once.
		ASSERT_THAT(AreEqual(
			0, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToWaypointReachedCallbackCanEndAbilityMidTick,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		// Same re-entrancy through the waypoint-reached callback, which fires
		// after the movement tick and before the order-progress tail.
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		const FSeinNavigationPayload Navigation =
			MakeEscapeNavigationComponent();
		FMoveToLifecycleFixture Fixture;
		Fixture.Destination = FFixedVector(
			FFixedPoint::FromInt(2000),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Observer->bEndAbilityOnWaypointReached = true;
		Fixture.Tick();

		ASSERT_THAT(AreEqual(1, Fixture.Observer->WaypointReachedCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
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

		const FSeinMovementPayload* Movement =
			Fixture.World->GetComponent<FSeinMovementPayload>(Fixture.Entity);
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
			Fixture.World->RemoveComponent<FSeinMovementPayload>(
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, &Navigation)));
		const FSeinEntityHandle Broker =
			Fixture.SeedFrozenDestinationLifecycle();
		ASSERT_THAT(IsTrue(Broker.IsValid()));

		Fixture.SetLocation(FFixedVector(
			Fixture.Destination.X - FFixedPoint(1),
			Fixture.Destination.Y,
			Fixture.Destination.Z));
		Fixture.Tick();

		const FSeinCommandBrokerData* BrokerData =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		const FSeinEntity* Member = Fixture.World->GetEntity(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Member));
		ASSERT_THAT(IsTrue(
			Member->Transform.GetLocation() == Fixture.Destination));
		ASSERT_THAT(AreEqual(
			1, BrokerData->SettledDestinationArtifact.Num()));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact[0].WorldPosition
				== Fixture.Destination));
	}

	TEST(MoveToOffDestinationCompletionDoesNotSettleFrozenDestination,
		"SeinARTS.Sim.Movement.FrozenDestination")
	{
		FScopedMoveToTestState Reset;
		USeinMoveToLifecycleTestMovement::bAdvanceInitialWaypointOnTick = true;
		FSeinNavigationPayload Navigation;
		Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(25);
		Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, &Navigation)));
		const FSeinEntityHandle Broker =
			Fixture.SeedFrozenDestinationLifecycle();
		ASSERT_THAT(IsTrue(Broker.IsValid()));

		for (int32 Tick = 0;
			Tick < 4 && !Fixture.Action->bCompleted;
			++Tick)
		{
			Fixture.Tick();
		}

		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		const FSeinEntity* Member = Fixture.World->GetEntity(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Member));
		ASSERT_THAT(IsTrue(
			Member->Transform.GetLocation() == FFixedVector::ZeroVector));
		const FSeinCommandBrokerData* BrokerData =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Broker);
		ASSERT_THAT(IsNotNull(BrokerData));
		ASSERT_THAT(IsTrue(
			BrokerData->SettledDestinationArtifact.IsEmpty()));
	}

	TEST(MoveToInitialPathFailureKeepsSettledFrozenDestination,
		"SeinARTS.Sim.Movement.FrozenDestination")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationPayload Navigation;
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
		FSeinNavigationPayload Navigation;
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
