// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Authoring/SeinEntityComponent.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Lib/SeinSelectionBPFL.h"
#include "Player/SeinCursorTrace.h"
#include "Player/SeinSelectionPolicy.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinSelectionPolicyTestTypes.h"

namespace
{
	struct FSelectionFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* Sim = nullptr;
		ASeinSelectionTestController* PC = nullptr;
		TArray<FSeinEntityHandle> Handles;
		TArray<ASeinActor*> Actors;
		~FSelectionFixture() { if (Sim) Sim->StopSimulation(); }
		bool Initialize()
		{
			UWorld& World = Spawner.GetWorld();
			Sim = World.GetSubsystem<USeinWorldSubsystem>();
			if (!Sim || !SeinTestMatchBootstrap::Materialize(*Sim, [&]()
			{
				Sim->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
				Sim->RegisterPlayer(FSeinPlayerID(2), FSeinFactionID(2));
				for (int32 I = 0; I < 5; ++I)
				{
					const FSeinEntityHandle Handle = Sim->SpawnEntity(
						I == 2 ? ASeinSelectionOtherTestActor::StaticClass() : ASeinActor::StaticClass(),
						FFixedTransform(), FSeinPlayerID(1));
					Handles.Add(Handle);
					FSeinExtentsPayload Extents;
					Extents.Shapes.AddDefaulted();
					Sim->AddComponent(Handle, Extents);
				}
			}, FSeinMatchSettings(), 0x53454C, TEXT("SelectionPolicy"))) return false;
			for (int32 I = 0; I < Handles.Num(); ++I)
			{
				ASeinActor* Actor = I == 2 ? &Spawner.SpawnActor<ASeinSelectionOtherTestActor>() : &Spawner.SpawnActor<ASeinActor>();
				Actor->InitializeWithEntity(Handles[I]);
				World.GetSubsystem<USeinActorBridgeSubsystem>()->RegisterActor(Handles[I], Actor);
				Actors.Add(Actor);
			}
			PC = &Spawner.SpawnActor<ASeinSelectionTestController>();
			PC->SeinPlayerID = FSeinPlayerID(1);
			return SeinTestMatchBootstrap::Start(*Sim);
		}
		void Edit(int32 Index, TFunctionRef<void(FSeinExtentsPayload&)> Change)
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*Sim);
			Change(*Sim->GetComponentMutable<FSeinExtentsPayload>(Handles[Index]));
		}
		TArray<ASeinActor*> Resolve(const TArray<ASeinActor*>& Incoming, const TArray<ASeinActor*>& Current = {}, bool bDrag = false)
		{
			return SeinSelectionPolicy::Resolve(Spawner.GetWorld(), FSeinPlayerID(1), Current, Incoming, bDrag);
		}
	};

	bool SelectionTrace(bool bParallel, TArray<FGuid>& Roots)
	{
		IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
		const int32 Previous = Cvar->GetInt();
		Cvar->SetWithCurrentPriority(bParallel ? 1 : 0);
		ON_SCOPE_EXIT { Cvar->SetWithCurrentPriority(Previous); };
		FSelectionFixture F;
		if (!F.Initialize()) return false;
		for (int32 Step = 0; Step < 5; ++Step)
		{
			F.Edit(Step, [Step](FSeinExtentsPayload& E)
			{
				E.SelectionPolicy = ESeinSelectionPolicy::LikeUnitsOnly;
				E.SelectionGroup = SeinARTSTags::Command_Context_Target_Ground;
				E.SelectionPriority = Step;
				E.bIncludeInDragSelection = false;
			});
			FTSTicker::GetCoreTicker().Tick(F.Sim->GetFixedDeltaTimeSeconds());
			FGuid Root;
			FString Error;
			if (F.Sim->GetCurrentTick() != Step + 1 || !F.Sim->ComputeCanonicalStateRoot(Root, Error)) return false;
			Roots.Add(Root);
			UE_LOG(LogTemp, Display, TEXT("[SelectionPolicyTrace] tick=%d root=%s"), Step + 1, *Root.ToString(EGuidFormats::Digits));
		}
		return true;
	}
}

namespace UE::SeinARTSTests
{
	TEST(DefaultsAndStablePriority, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(AreEqual(5, F.Resolve(F.Actors).Num()));
		F.Edit(2, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::SingleOnly; E.SelectionPriority = 20; });
		const auto Selected = F.Resolve({F.Actors[4], F.Actors[0], F.Actors[2], F.Actors[2]});
		ASSERT_THAT(AreEqual(1, Selected.Num()));
		ASSERT_THAT(IsTrue(Selected[0] == F.Actors[2]));
		F.Edit(2, [](auto& E) { E.SelectionPriority = 0; });
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[2], F.Actors[0]}) == F.Resolve({F.Actors[0], F.Actors[2]})));
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[2]}, {F.Actors[0]}) == TArray<ASeinActor*>{F.Actors[0]}));
	}

	TEST(LikeUnitsUsesExactClassOrExactGroup, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Edit(0, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::LikeUnitsOnly; });
		ASSERT_THAT(AreEqual(2, F.Resolve({F.Actors[0], F.Actors[1], F.Actors[2]}).Num()));
		// A restrictive incoming unit cannot join an already mixed unrestricted selection.
		ASSERT_THAT(AreEqual(2, F.Resolve({F.Actors[0]}, {F.Actors[1], F.Actors[2]}).Num()));
		for (int32 I : {0, 2}) F.Edit(I, [](auto& E) { E.SelectionGroup = SeinARTSTags::Command_Context_Target_Ground; });
		ASSERT_THAT(AreEqual(2, F.Resolve({F.Actors[0], F.Actors[1], F.Actors[2]}).Num()));
		F.Edit(2, [](auto& E) { E.SelectionGroup = SeinARTSTags::Command_Context_Target_Ground.GetTag().RequestDirectParent(); });
		ASSERT_THAT(AreEqual(1, F.Resolve({F.Actors[0], F.Actors[2]}).Num()));
	}

	TEST(ControllerClickDragAndGroupsSharePolicy, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Edit(0, [](auto& E) { E.bIncludeInDragSelection = false; });
		F.PC->ReceiveMarqueeSelection({F.Actors[0], F.Actors[1]});
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors() == TArray<ASeinActor*>{F.Actors[1]}));
		F.PC->SetSelection({F.Actors[0]});
		F.PC->SetShift(true);
		F.PC->ReceiveMarqueeSelection({F.Actors[1]});
		ASSERT_THAT(AreEqual(2, F.PC->GetValidSelectedActors().Num()));
		F.PC->AssignControlGroup(1);
		F.PC->ClearSelection();
		F.PC->RecallControlGroup(1);
		ASSERT_THAT(AreEqual(2, F.PC->GetValidSelectedActors().Num()));
		F.Edit(2, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::SingleOnly; });
		F.PC->ToggleSelection(F.Actors[2]);
		ASSERT_THAT(AreEqual(2, F.PC->GetValidSelectedActors().Num()));
		F.PC->SetSelection({F.Actors[2]});
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors() == TArray<ASeinActor*>{F.Actors[2]}));
		F.PC->AddToSelection({F.Actors[0]});
		ASSERT_THAT(AreEqual(1, F.PC->GetValidSelectedActors().Num()));
		F.Edit(0, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::Disabled; });
		F.PC->RecallControlGroup(1);
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors() == TArray<ASeinActor*>{F.Actors[1]}));
	}

	TEST(EligibilityAndBulkActions, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Edit(0, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::Disabled; });
		F.Edit(1, [](auto& E) { E.Shapes.Reset(); });
		F.Actors[2]->SetActorHiddenInGame(true);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			USeinSelectionBPFL::SeinSetSelectable(F.Sim, F.Handles[3], false);
		}
		F.PC->HandleSelectAll(false);
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors() == TArray<ASeinActor*>{F.Actors[4]}));
		F.PC->HoveredActor = F.Actors[4];
		F.PC->HandleSelectAllOfType(false);
		ASSERT_THAT(AreEqual(1, F.PC->GetValidSelectedActors().Num()));
		F.PC->SeinPlayerID = FSeinPlayerID(2);
		F.PC->SetSelection(F.Actors);
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors().IsEmpty()));
		FHitResult Hit;
		ASSERT_THAT(IsTrue(SeinCursorTrace::TraceEntities(F.Spawner.GetWorld(), FVector(0, 0, 1000), -FVector::UpVector, 2000, Hit)));
		ASSERT_THAT(IsTrue(Hit.GetActor() == F.Actors[0])); // Disabled still supports targeting.
	}

	TEST(SquadPolicyAndRuntimeFocus, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			FSeinSquadMemberPayload Member;
			Member.SquadEntity = F.Handles[2];
			F.Sim->AddComponent(F.Handles[0], Member);
			USeinSelectionBPFL::SeinSetSelectable(F.Sim, F.Handles[0], false);
		}
		F.Edit(0, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::Disabled; });
		F.Edit(2, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::SingleOnly; });
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[0]}) == TArray<ASeinActor*>{F.Actors[2]}));
		F.Edit(2, [](auto& E) { E.bIncludeInDragSelection = false; });
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[0]}, {}, true).IsEmpty()));
		F.Edit(2, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::Unrestricted; });
		F.PC->SetSelection({F.Actors[1], F.Actors[2], F.Actors[3]});
		F.PC->ActiveFocusIndex = 1;
		F.PC->ToggleSelection(F.Actors[1]);
		ASSERT_THAT(IsTrue(F.PC->GetFocusedActor() == F.Actors[2]));
		F.Edit(2, [](auto& E) { E.SelectionPolicy = ESeinSelectionPolicy::Disabled; });
		F.PC->PurgeStaleSelection();
		ASSERT_THAT(IsTrue(F.PC->GetFocusedActor() == nullptr));
		ASSERT_THAT(IsTrue(F.PC->GetValidSelectedActors() == TArray<ASeinActor*>{F.Actors[3]}));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			FSeinSquadMemberPayload Member;
			Member.SquadEntity = F.Handles[0];
			F.Sim->AddComponent(F.Handles[2], Member);
		}
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[0]}).IsEmpty())); // Cycle fails closed.
	}

	TEST(PlacedAndProducedActorsDefaultSelectableAbstractsOptIn, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(USeinSelectionBPFL::SeinIsSelectable(F.Sim, F.Handles[0])));
		ASeinActor& Placed = F.Spawner.SpawnActor<ASeinActor>();
		Placed.bSimLocationBaked = true;
		Placed.bSimRotationBaked = true;
		FSeinEntityHandle PlacedHandle, AbstractHandle;
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			PlacedHandle = F.Sim->SpawnEntityFromPlacedActor(&Placed, FSeinPlayerID::Neutral());
			AbstractHandle = F.Sim->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
		}
		ASSERT_THAT(IsTrue(PlacedHandle.IsValid()));
		ASSERT_THAT(IsTrue(USeinSelectionBPFL::SeinIsSelectable(F.Sim, PlacedHandle)));
		ASSERT_THAT(IsFalse(USeinSelectionBPFL::SeinIsSelectable(F.Sim, AbstractHandle)));
		FSeinEntityHandle Reused;
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			USeinSelectionBPFL::SeinSetSelectable(F.Sim, F.Handles[0], false);
			F.Sim->DestroyEntity(F.Handles[0]);
		}
		// Destruction releases the slot at the fixed-tick teardown boundary.
		FTSTicker::GetCoreTicker().Tick(F.Sim->GetFixedDeltaTimeSeconds());
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			Reused = F.Sim->SpawnEntity(ASeinActor::StaticClass(), FFixedTransform(), FSeinPlayerID(1));
		}
		ASSERT_THAT(AreEqual(F.Handles[0].Index, Reused.Index));
		ASSERT_THAT(IsTrue(F.Handles[0] != Reused));
		ASSERT_THAT(IsTrue(USeinSelectionBPFL::SeinIsSelectable(F.Sim, Reused)));
	}

	TEST(StaleActorsAndMissingExtentsFailClosed, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		FActorTestSpawner OtherSpawner;
		ASeinActor& Foreign = OtherSpawner.SpawnActor<ASeinActor>();
		Foreign.InitializeWithEntity(F.Handles[0]);
		ASSERT_THAT(IsTrue(F.Resolve({&Foreign}).IsEmpty()));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			F.Sim->RemoveComponent<FSeinExtentsPayload>(F.Handles[0]);
			F.Sim->DestroyEntity(F.Handles[1]);
		}
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[0], F.Actors[1], nullptr}).IsEmpty()));
		ASeinActor& Duplicate = F.Spawner.SpawnActor<ASeinActor>();
		Duplicate.InitializeWithEntity(F.Handles[2]);
		ASSERT_THAT(IsTrue(F.Resolve({&Duplicate}).IsEmpty()));
		F.Edit(3, [](auto& E) { E.SelectionPolicy = static_cast<ESeinSelectionPolicy>(255); });
		ASSERT_THAT(IsTrue(F.Resolve({F.Actors[3]}).IsEmpty()));
	}

	TEST(AuthoringPayloadAndCanonicalFields, "SeinARTS.Unit.Selection.Policy")
	{
		FSelectionFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		USeinExtentsComponent* Component = NewObject<USeinExtentsComponent>();
		Component->Extents.SelectionPolicy = ESeinSelectionPolicy::SingleOnly;
		Component->Extents.bIncludeInDragSelection = false;
		Component->Extents.SelectionGroup = SeinARTSTags::Command_Context_Target_Ground;
		Component->Extents.SelectionPriority = 17;
		FInstancedStruct Baked;
		ASSERT_THAT(IsTrue(Component->WritePayload(Baked)));
		ASSERT_THAT(AreEqual(GetTypeHash(Component->Extents), GetTypeHash(Baked.Get<FSeinExtentsPayload>())));
		FGuid Before, After;
		FString Error;
		ASSERT_THAT(IsTrue(F.Sim->ComputeCanonicalStateRoot(Before, Error)));
		for (int32 Field = 0; Field < 4; ++Field)
		{
			const uint32 OldHash = GetTypeHash(*F.Sim->GetComponent<FSeinExtentsPayload>(F.Handles[0]));
			F.Edit(0, [Field](auto& E)
			{
				if (Field == 0) E.SelectionPolicy = ESeinSelectionPolicy::LikeUnitsOnly;
				if (Field == 1) E.bIncludeInDragSelection = false;
				if (Field == 2) E.SelectionGroup = SeinARTSTags::Command_Context_Target_Ground;
				if (Field == 3) E.SelectionPriority = -9;
			});
			ASSERT_THAT(IsTrue(OldHash != GetTypeHash(*F.Sim->GetComponent<FSeinExtentsPayload>(F.Handles[0]))));
			ASSERT_THAT(IsTrue(F.Sim->ComputeCanonicalStateRoot(After, Error)));
			ASSERT_THAT(IsTrue(Before != After));
			Before = After;
		}
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.Sim);
			USeinSelectionBPFL::SeinSetSelectable(F.Sim, F.Handles[0], false);
		}
		FTSTicker::GetCoreTicker().Tick(F.Sim->GetFixedDeltaTimeSeconds());
		F.Sim->StopSimulation();
		FSeinWorldSnapshot Snapshot;
		F.Sim->CaptureSnapshot(Snapshot);
		FActorTestSpawner OtherSpawner;
		USeinWorldSubsystem* Other = OtherSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Other, Snapshot)));
		ASSERT_THAT(IsFalse(USeinSelectionBPFL::SeinIsSelectable(Other, F.Handles[0])));
		ASSERT_THAT(IsTrue(F.Sim->StartSimulation()));
		ASSERT_THAT(IsTrue(Other->StartSimulation()));
		ON_SCOPE_EXIT { Other->StopSimulation(); };
		for (int32 Step = 0; Step < 3; ++Step)
		{
			FTSTicker::GetCoreTicker().Tick(F.Sim->GetFixedDeltaTimeSeconds());
			ASSERT_THAT(IsTrue(F.Sim->ComputeCanonicalStateRoot(Before, Error)));
			ASSERT_THAT(IsTrue(Other->ComputeCanonicalStateRoot(After, Error)));
			ASSERT_THAT(IsTrue(Before == After));
		}
	}

	TEST(SerialSelectionTrace, "SeinARTS.Unit.Selection.Policy.Process")
	{
		TArray<FGuid> Roots;
		ASSERT_THAT(IsTrue(SelectionTrace(false, Roots)));
	}
	TEST(ParallelSelectionTrace, "SeinARTS.Unit.Selection.Policy.Process")
	{
		TArray<FGuid> Roots;
		ASSERT_THAT(IsTrue(SelectionTrace(true, Roots)));
	}
}
