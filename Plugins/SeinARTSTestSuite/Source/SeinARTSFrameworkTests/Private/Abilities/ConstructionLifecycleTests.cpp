// SeinARTS construction lifecycle regression coverage.
#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinConstructionPayload.h"
#include "Widget/SeinEntityWidget.h"
#include "Components/SeinActiveEffectsPayload.h"
#include "Components/ActorComponents/SeinConstructionRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/WidgetComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinConstructionBPFL.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinEconomyLoopTestTypes.h"
#include "TestTypes/SeinConstructionTestTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Containers/Ticker.h"

namespace UE::SeinARTSTests
{
namespace ConstructionTestLocal
{
	struct FFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		FSeinEntityHandle Entity;
		bool Initialize()
		{
			return SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
				Entity = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID(1));
				FSeinConstructionPayload Data;

				Data.CompletionEffect = USeinConstructionTestEffect::StaticClass();
				World->AddComponent(Entity, Data);
				World->AddComponent(Entity, FSeinActiveEffectsPayload());
			}) && SeinTestMatchBootstrap::Start(*World);
		}
		FSeinConstructionHandle Queue()
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			FSeinConstructionHandle Job;
			USeinConstructionBPFL::SeinQueueConstruction(World, Entity, Job);
			return Job;
		}
		~FFixture() { if (World) World->StopSimulation(); }
	};
}


	TEST(ConstructionCompletesWithoutContributingWork, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		const auto Job = F.Queue();
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity).bValid));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinStartConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinPauseConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job) == ESeinConstructionResult::AlreadyComplete));
	}

	TEST(WorkThresholdAndCompletionAreIndependent, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto Job = F.Queue();
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
		USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(3));
		USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity).Progress == FFixedPoint::FromInt(3)));
		USeinConstructionBPFL::SeinQueueConstruction(F.World, F.Entity, Job);
		USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::MaxValue) == ESeinConstructionResult::ReadyToComplete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(F.World, F.Entity).State == ESeinConstructionState::Building));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity).bWorkComplete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinPauseConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
	}

	TEST(RequiredWorkEditsApplyToNextJobOnly, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto Job = F.Queue();
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
		USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(3));
		F.World->GetComponentMutable<FSeinConstructionPayload>(F.Entity)->RequiredWork = FFixedPoint::FromInt(20);
		FSeinConstructionHandle SameJob;
		USeinConstructionBPFL::SeinQueueConstruction(F.World, F.Entity, SameJob);
		ASSERT_THAT(AreEqual(Job.JobID, SameJob.JobID));
		auto Work = USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity);
		ASSERT_THAT(IsTrue(Work.RequiredWork == FFixedPoint::FromInt(10)));
		ASSERT_THAT(IsTrue(Work.Progress == FFixedPoint::FromInt(3)));
		USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job);
		USeinConstructionBPFL::SeinQueueConstruction(F.World, F.Entity, Job);
		Work = USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity);
		ASSERT_THAT(IsTrue(Work.RequiredWork == FFixedPoint::FromInt(20)));
		ASSERT_THAT(IsTrue(Work.Progress == FFixedPoint::Zero));
	}

	TEST(WidgetContextCanBeReusedAndCleared, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		const auto Job = F.Queue();
		auto* Widget = CreateWidget<USeinConstructionWorkProgressWidget>(&F.Spawner.GetWorld());
		ASSERT_THAT(IsNotNull(Widget));
		Widget->SetEntityContext(F.Entity);
		ASSERT_THAT(IsTrue(Widget->GetProgressDisplay().bVisible));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
			USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(5));
		}
		ASSERT_THAT(IsNear(0.5f, Widget->GetProgressDisplay().Percent, 0.001f));
		Widget->SetEntityContext(FSeinEntityHandle::Invalid());
		ASSERT_THAT(IsFalse(Widget->BoundEntity.IsValid()));
		ASSERT_THAT(IsFalse(Widget->GetProgressDisplay().bVisible));
		Widget->SetEntityContext(F.Entity);
		ASSERT_THAT(IsNear(0.5f, Widget->GetProgressDisplay().Percent, 0.001f));
	}

	TEST(ExplicitPhasesAndStableCompletion, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionPercent(F.World, F.Entity) == FFixedPoint::Zero));
		auto Job = F.Queue();
		ASSERT_THAT(IsTrue(Job.JobID == 1));
		{
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(F.World->HasTag(F.Entity, SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::One) == ESeinConstructionResult::InvalidState));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinStartConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(3)) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinPauseConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::One) == ESeinConstructionResult::InvalidState));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity).Progress == FFixedPoint::FromInt(3)));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinSetConstructionStage(F.World, Job, SeinARTSTags::State) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinStartConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(F.World, F.Entity).State == ESeinConstructionState::Building));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::MaxValue) == ESeinConstructionResult::ReadyToComplete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinIsUnderConstruction(F.World, F.Entity)));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job) == ESeinConstructionResult::AlreadyComplete));
		auto Status = USeinConstructionBPFL::SeinGetConstructionStatus(F.World, F.Entity);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionWorkStatus(F.World, F.Entity).Percent == FFixedPoint::One));
		ASSERT_THAT(IsTrue(Status.Stage == SeinARTSTags::State));
		ASSERT_THAT(IsNotNull(F.World->GetComponent<FSeinConstructionPayload>(F.Entity)));
		ASSERT_THAT(IsFalse(F.World->HasTag(F.Entity, SeinARTSTags::State_UnderConstruction)));
		}
		// Runtime effects commit at the next PreTick. Independent instances expose duplicate application.
		FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(1, F.World->GetComponent<FSeinActiveEffectsPayload>(F.Entity)->ActiveEffects.Num()));
		FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(1, F.World->GetComponent<FSeinActiveEffectsPayload>(F.Entity)->ActiveEffects.Num()));
	}

	TEST(StaleJobsRepeatedQueueAndRejectedInputsDoNotMutate, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto Job = F.Queue();
		FSeinConstructionHandle Same;
		{
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinQueueConstruction(F.World, F.Entity, Same) == ESeinConstructionResult::Unchanged));
		ASSERT_THAT(AreEqual(Job.JobID, Same.JobID));
		USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
		}
		FGuid Before, After; FString Error;
		ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(Before, Error)));
		{
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::Zero) == ESeinConstructionResult::InvalidAmount));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, -FFixedPoint::One) == ESeinConstructionResult::InvalidAmount));
		}
		ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(After, Error)));
		ASSERT_THAT(IsTrue(Before == After));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		USeinConstructionBPFL::SeinForceCompleteConstruction(F.World, Job);
		FSeinConstructionHandle Next;
		USeinConstructionBPFL::SeinQueueConstruction(F.World, F.Entity, Next);
		ASSERT_THAT(AreEqual(Job.JobID + 1, Next.JobID));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinStartConstruction(F.World, Job) == ESeinConstructionResult::StaleJob));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinSetConstructionStage(F.World, Job, SeinARTSTags::State) == ESeinConstructionResult::StaleJob));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinForceCompleteConstruction(F.World, Job) == ESeinConstructionResult::StaleJob));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(F.World, F.Entity).State == ESeinConstructionState::Queued));
	}

	TEST(ZeroTimeNeedsExplicitStartAndComplete, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		F.World->GetComponentMutable<FSeinConstructionPayload>(F.Entity)->RequiredWork = FFixedPoint::Zero;
		auto Job = F.Queue();
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionPercent(F.World, F.Entity) == FFixedPoint::One));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinStartConstruction(F.World, Job) == ESeinConstructionResult::ReadyToComplete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinIsUnderConstruction(F.World, F.Entity)));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinCompleteConstruction(F.World, Job) == ESeinConstructionResult::Succeeded));
	}

	TEST(FreshWorldRestorePreservesPausedStageAndNextJobIdentity, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto Job = F.Queue();
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
			USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(4));
			USeinConstructionBPFL::SeinSetConstructionStage(F.World, Job, SeinARTSTags::State);
			USeinConstructionBPFL::SeinPauseConstruction(F.World, Job);
		}
		FSeinWorldSnapshot Snapshot; F.World->CaptureSnapshot(Snapshot);
		FActorTestSpawner RestoredSpawner;
		auto* Restored = RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Restored, Snapshot)));
		for (auto* World : {F.World, Restored})
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			const auto Status = USeinConstructionBPFL::SeinGetConstructionStatus(World, F.Entity);
			ASSERT_THAT(IsTrue(Status.State == ESeinConstructionState::Paused));
			ASSERT_THAT(IsTrue(Status.Stage == SeinARTSTags::State));
			USeinConstructionBPFL::SeinStartConstruction(World, Job);
			USeinConstructionBPFL::SeinAdvanceConstruction(World, Job, FFixedPoint::FromInt(6));
			USeinConstructionBPFL::SeinCompleteConstruction(World, Job);
			FSeinConstructionHandle Next;
			USeinConstructionBPFL::SeinQueueConstruction(World, F.Entity, Next);
			ASSERT_THAT(AreEqual(Job.JobID + 1, Next.JobID));
		}
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
			FGuid A, B; FString Error;
			ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(A, Error)));
			ASSERT_THAT(IsTrue(Restored->ComputeCanonicalStateRoot(B, Error)));
			ASSERT_THAT(IsTrue(A == B));
		}
		Restored->StopSimulation();
	}

	TEST(SpawnDefaultsAndExplicitConstructionSite, "SeinARTS.Sim.Construction")
	{
		FActorTestSpawner Spawner;
		auto* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		TArray<const USeinEntityBridgeComponent*> Bridges;
		AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(ASeinEconomyConstructionTestActor::StaticClass(), Bridges);
		auto* Bridge = const_cast<USeinEntityBridgeComponent*>(Bridges[0]);
		const auto Previous = Bridge->ComponentData;
		Bridge->ComponentData = {FInstancedStruct::Make(FSeinConstructionPayload())};
		FSeinEntityHandle Complete, Queued, Building;
		FSeinConstructionHandle Job;
		const bool bMaterialized = SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			World->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
			Complete = World->SpawnEntity(ASeinEconomyConstructionTestActor::StaticClass(), FFixedTransform(), FSeinPlayerID(1));
			Queued = USeinConstructionBPFL::SeinSpawnConstructionSite(World, ASeinEconomyConstructionTestActor::StaticClass(),
				FFixedTransform(), FSeinPlayerID(1), ESeinConstructionInitialState::Queued, Job);
			Building = USeinConstructionBPFL::SeinSpawnConstructionSite(World, ASeinEconomyConstructionTestActor::StaticClass(),
				FFixedTransform(), FSeinPlayerID(1), ESeinConstructionInitialState::Building, Job);
		});
		Bridge->ComponentData = Previous;
		ASSERT_THAT(IsTrue(bMaterialized));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(World, Complete).State == ESeinConstructionState::Complete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(World, Queued).State == ESeinConstructionState::Queued));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(World, Building).State == ESeinConstructionState::Building));
		ASSERT_THAT(IsFalse(World->HasTag(Complete, SeinARTSTags::State_UnderConstruction)));
	}

	TEST(DemoConstructionGroupsResolveOnInstantiatedBuildings, "SeinARTS.Sim.Construction")
	{
		FActorTestSpawner Spawner;
		for (const TCHAR* Name : {TEXT("Barracks"), TEXT("Factory")})
		{
			const FString Path = FString(TEXT("/SeinARTSFramework/Demo/Blueprints/SU_")) + Name + TEXT(".SU_") + Name + TEXT("_C");
			auto* Class = LoadClass<ASeinActor>(nullptr, *Path);
			ASSERT_THAT(IsNotNull(Class));
			auto* Actor = Spawner.GetWorld().SpawnActor<ASeinActor>(Class);
			ASSERT_THAT(IsNotNull(Actor));
			auto* Renderer = Actor->FindComponentByClass<USeinConstructionRenderComponent>();
			ASSERT_THAT(IsNotNull(Renderer));
			ASSERT_THAT(AreEqual(2, Renderer->Groups.Num()));
			TArray<UActorComponent*> ActorComponents;
			Actor->GetComponents(ActorComponents);
			for (const auto* Component : ActorComponents)
				ASSERT_THAT(IsFalse(Component->GetClass()->GetName() == TEXT("SeinConstructionWorkComponent")));
			TSet<UActorComponent*> Members;
			for (const auto& Group : Renderer->Groups)
			{
				ASSERT_THAT(IsTrue(Group.ActorClass == nullptr));
				ASSERT_THAT(AreEqual(1, Group.Components.Num()));
				for (const auto& Reference : Group.Components)
				{
					auto* Component = Reference.GetComponent(Actor);
					ASSERT_THAT(IsNotNull(Component));
					ASSERT_THAT(IsTrue(Component->IsA<UStaticMeshComponent>()));
					Members.Add(Component);
				}
			}
			ASSERT_THAT(AreEqual(2, Members.Num()));
			const auto* Bridge = Actor->FindComponentByClass<USeinEntityBridgeComponent>();
			ASSERT_THAT(IsNotNull(Bridge));
			bool bHasWork = false;
			for (const auto& Data : Bridge->ComponentData)
			{
				if (const auto* Work = Data.GetPtr<FSeinConstructionPayload>()) bHasWork = Work->RequiredWork > FFixedPoint::Zero;
				if (const auto* Construction = Data.GetPtr<FSeinConstructionPayload>()) ASSERT_THAT(IsFalse(Construction->bQueueConstructionOnSpawn));
			}
			ASSERT_THAT(IsTrue(bHasWork));
		}
	}

	TEST(SamePhaseRestoreRefreshesWorkWithoutTransitionEvents, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto& Actor = F.Spawner.SpawnActor<ASeinEconomyConstructionTestActor>();
		auto* Renderer = NewObject<USeinConstructionRenderComponent>(&Actor);
		Actor.AddInstanceComponent(Renderer); Renderer->RegisterComponent();
		Actor.InitializeWithEntity(F.Entity);
		F.Spawner.GetWorld().GetSubsystem<USeinActorBridgeSubsystem>()->RegisterActor(F.Entity, &Actor);
		if (!Actor.HasActorBegunPlay()) Actor.DispatchBeginPlay();
		TStrongObjectPtr<USeinConstructionEventProbe> Probe(NewObject<USeinConstructionEventProbe>());
		Probe->Renderer = Renderer;
		Renderer->OnPresentationRefresh.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::Refreshed);
		Renderer->OnConstructionStateChanged.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::StateChanged);
		const auto Job = F.Queue();
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
			USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(2));
		}
		for (const auto& Event : F.World->FlushVisualEvents()) Renderer->HandleVisualEvent(Event);
		FSeinWorldSnapshot Snapshot; F.World->CaptureSnapshot(Snapshot);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinAdvanceConstruction(F.World, Job, FFixedPoint::FromInt(5));
		}
		Renderer->RefreshEntityBinding();
		ASSERT_THAT(IsTrue(Probe->RefreshedWork == FFixedPoint::FromInt(7)));
		const int32 RefreshCount = Probe->RefreshCount;
		const int32 TransitionCount = Probe->NewStates.Num();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*F.World, Snapshot)));
		ASSERT_THAT(IsTrue(Probe->RefreshCount > RefreshCount));
		ASSERT_THAT(IsTrue(Probe->RefreshedWork == FFixedPoint::FromInt(2)));
		ASSERT_THAT(AreEqual(TransitionCount, Probe->NewStates.Num()));
	}

	TEST(CompletionListenerRebindingCannotApplyOldEntityPresentation, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		FSeinEntityHandle Other;
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			Other = F.World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID(1));
			F.World->AddComponent(Other, FSeinConstructionPayload());
			FSeinConstructionHandle Job;
			USeinConstructionBPFL::SeinQueueConstruction(F.World, Other, Job);
		}
		auto& Actor = F.Spawner.SpawnActor<ASeinEconomyConstructionTestActor>();
		auto& OtherActor = F.Spawner.SpawnActor<ASeinEconomyConstructionTestActor>();
		Actor.InitializeWithEntity(F.Entity); OtherActor.InitializeWithEntity(Other);
		auto* Renderer = NewObject<USeinConstructionRenderComponent>(&Actor);
		Actor.AddInstanceComponent(Renderer); Renderer->RegisterComponent();
		if (!Actor.HasActorBegunPlay()) Actor.DispatchBeginPlay();
		TStrongObjectPtr<USeinConstructionEventProbe> Probe(NewObject<USeinConstructionEventProbe>());
		Probe->Renderer = Renderer; Probe->RebindTarget = &OtherActor;
		Renderer->OnConstructionStateChanged.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::StateChanged);
		Renderer->OnBoundVisualEvent.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::VisualEvent);
		Renderer->HandleVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(F.Entity, false));
		ASSERT_THAT(IsTrue(Renderer->GetEntityHandle() == Other));
		ASSERT_THAT(IsFalse(Renderer->IsPresentationGroupVisible(Renderer->FinishedGroup)));
		ASSERT_THAT(IsTrue(Renderer->IsPresentationGroupVisible(Renderer->ConstructionGroup)));
		ASSERT_THAT(AreEqual(0, Probe->EventCount));
	}

	TEST(RendererPreservesWidgetsAndDeliversOrderedTransitions, "SeinARTS.Sim.Construction")
	{
		ConstructionTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		auto& Actor = F.Spawner.SpawnActor<ASeinEconomyConstructionTestActor>();
		auto* Renderer = NewObject<USeinConstructionRenderComponent>(&Actor);
		Actor.AddInstanceComponent(Renderer); Renderer->RegisterComponent();
		auto* Mesh = NewObject<UStaticMeshComponent>(&Actor);
		Mesh->SetMobility(EComponentMobility::Static);
		Actor.SetRootComponent(Mesh);
		Actor.AddInstanceComponent(Mesh); Mesh->RegisterComponent(); Mesh->SetVisibility(true);
		auto* Widget = NewObject<UWidgetComponent>(&Actor);
		Actor.AddInstanceComponent(Widget); Widget->RegisterComponent(); Widget->SetVisibility(true);
		FComponentReference FinishedMesh; FinishedMesh.OverrideComponent = Mesh;
		Renderer->Groups[0].Components.Add(FinishedMesh);
		Renderer->Groups[1].ActorClass = ASeinConstructionManagedTestActor::StaticClass();
		Actor.InitializeWithEntity(F.Entity);
		F.Spawner.GetWorld().GetSubsystem<USeinActorBridgeSubsystem>()->RegisterActor(F.Entity, &Actor);
		if (!Actor.HasActorBegunPlay()) Actor.DispatchBeginPlay();
		TStrongObjectPtr<USeinConstructionEventProbe> Probe(NewObject<USeinConstructionEventProbe>());
		Renderer->OnConstructionStateChanged.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::StateChanged);
		Renderer->OnConstructionStageChanged.AddDynamic(Probe.Get(), &USeinConstructionEventProbe::StageChanged);
		F.World->FlushVisualEvents();
		auto Job = F.Queue();
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinStartConstruction(F.World, Job);
			USeinConstructionBPFL::SeinSetConstructionStage(F.World, Job, SeinARTSTags::State);
			USeinConstructionBPFL::SeinSetConstructionStage(F.World, Job, SeinARTSTags::State_UnderConstruction);
		}
		for (const auto& Event : F.World->FlushVisualEvents()) Renderer->HandleVisualEvent(Event);
		ASSERT_THAT(AreEqual(2, Probe->NewStates.Num()));
		ASSERT_THAT(IsTrue(Probe->NewStates[0] == ESeinConstructionState::Queued));
		ASSERT_THAT(IsTrue(Probe->NewStates[1] == ESeinConstructionState::Building));
		ASSERT_THAT(AreEqual(2, Probe->NewStages.Num()));
		ASSERT_THAT(IsTrue(Probe->OldStages[1] == SeinARTSTags::State));
		ASSERT_THAT(IsFalse(Mesh->IsVisible()));
		ASSERT_THAT(IsTrue(Widget->IsVisible()));
		TArray<AActor*> Children; Actor.GetAttachedActors(Children);
		ASSERT_THAT(AreEqual(1, Children.Num()));
		ASSERT_THAT(IsTrue(Children[0]->GetOwner() == &Actor));
		auto* Managed = Cast<ASeinConstructionManagedTestActor>(Children[0]);
		ASSERT_THAT(IsNotNull(Managed));
		ASSERT_THAT(IsTrue(Managed->bContextReadyAtBeginPlay));
		ASSERT_THAT(IsTrue(Managed->EntityAtBeginPlay == F.Entity));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinPauseConstruction(F.World, Job);
		}
		for (const auto& Event : F.World->FlushVisualEvents()) Renderer->HandleVisualEvent(Event);
		FSeinWorldSnapshot PausedSnapshot;
		F.World->CaptureSnapshot(PausedSnapshot);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinConstructionBPFL::SeinForceCompleteConstruction(F.World, Job);
		}
		for (const auto& Event : F.World->FlushVisualEvents()) Renderer->HandleVisualEvent(Event);
		ASSERT_THAT(IsTrue(Probe->NewStates.Last() == ESeinConstructionState::Complete));
		ASSERT_THAT(IsTrue(Mesh->IsVisible()));
		Children.Reset(); Actor.GetAttachedActors(Children);
		ASSERT_THAT(AreEqual(0, Children.Num()));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*F.World, PausedSnapshot)));
		ASSERT_THAT(IsFalse(Mesh->IsVisible()));
		ASSERT_THAT(IsTrue(Widget->IsVisible()));
		ASSERT_THAT(IsTrue(Probe->NewStates.Last() == ESeinConstructionState::Complete));
		ASSERT_THAT(IsTrue(USeinConstructionBPFL::SeinGetConstructionStatus(F.World, F.Entity).Stage == SeinARTSTags::State_UnderConstruction));
		Children.Reset(); Actor.GetAttachedActors(Children);
		ASSERT_THAT(AreEqual(1, Children.Num()));
		for (const auto& Event : F.World->FlushVisualEvents())
			ASSERT_THAT(IsTrue(Event.Type != ESeinVisualEventType::EffectApplied));
		Renderer->HandleVisualEvent(FSeinVisualEvent::MakeConstructionStateChangedEvent(F.Entity, true));
		Renderer->HandleVisualEvent(FSeinVisualEvent::MakeDestroyEvent(F.Entity));
		Children.Reset(); Actor.GetAttachedActors(Children);
		ASSERT_THAT(AreEqual(0, Children.Num()));
	}
}
