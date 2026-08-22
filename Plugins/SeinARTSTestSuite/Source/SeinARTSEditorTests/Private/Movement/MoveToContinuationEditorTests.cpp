#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Logging/TokenizedMessage.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "SeinMovementSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
#include "TestTypes/SeinMoveToContinuationEditorTestTypes.h"
#include "UObject/FieldIterator.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace UE::SeinARTSTests
{
	namespace MoveToContinuationEditor
	{
		const FName EntryPointName(
			TEXT("RunMoveToContinuationProbe"));

		struct FRoute
		{
			FName Channel;
			FName Recorder;
			FSeinMoveToDelegate USeinMoveToProxy::* Delegate =
				nullptr;
		};

		const FRoute* Routes()
		{
			static const FRoute Values[] = {
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnCompleted),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordCompleted),
					&USeinMoveToProxy::OnCompleted
				},
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnFailed),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordFailed),
					&USeinMoveToProxy::OnFailed
				},
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnWaypointReached),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordWaypoint),
					&USeinMoveToProxy::OnWaypointReached
				},
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnCancelled),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordCancelled),
					&USeinMoveToProxy::OnCancelled
				},
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnPartialPath),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordPartialPath),
					&USeinMoveToProxy::OnPartialPath
				},
				{
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy, OnPathRecomputed),
					GET_FUNCTION_NAME_CHECKED(
						USeinMoveToContinuationEditorTestAbility,
						RecordPathRecomputed),
					&USeinMoveToProxy::OnPathRecomputed
				},
			};
			return Values;
		}

		constexpr int32 RouteCount = 6;

		struct FScopedEscapeNavigation
		{
			FScopedEscapeNavigation()
				: Settings(GetMutableDefault<USeinARTSCoreSettings>())
				, SavedNavigationClass(Settings
					? Settings->NavigationClass
					: FSoftClassPath())
			{
				check(Settings);
				USeinMoveToContinuationEditorTestNavigation::Reset();
				Settings->NavigationClass = FSoftClassPath(
					USeinMoveToContinuationEditorTestNavigation::
						StaticClass()->GetPathName());
			}

			~FScopedEscapeNavigation()
			{
				Settings->NavigationClass = SavedNavigationClass;
				USeinMoveToContinuationEditorTestNavigation::Reset();
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedNavigationClass;
		};

		void AddNode(UEdGraph& Graph, UEdGraphNode& Node)
		{
			Graph.AddNode(&Node, false, false);
			Node.CreateNewGuid();
			Node.PostPlacedNewNode();
			Node.AllocateDefaultPins();
		}

		struct FCompiledBlueprint
		{
			TStrongObjectPtr<UBlueprint> Asset;
			UClass* Class = nullptr;
			FSeinPoolObjectLocalClassAdmissionHandle
				PoolClassAdmission;
			TArray<TMap<FName, FName>> CallbackSets;
		};

		bool CompileBlueprint(
			FCompiledBlueprint& Out,
			FString& OutError,
			bool bSequential = false)
		{
			Out = {};
			OutError.Reset();
			const FName BlueprintName =
				MakeUniqueObjectName(
					GetTransientPackage(),
					UBlueprint::StaticClass(),
					TEXT("BP_SeinMoveToContinuationProbe"));
			UBlueprint* Blueprint =
				FKismetEditorUtilities::CreateBlueprint(
					USeinMoveToContinuationEditorTestAbility::
						StaticClass(),
					GetTransientPackage(),
					BlueprintName,
					BPTYPE_Normal,
					UBlueprint::StaticClass(),
					UBlueprintGeneratedClass::StaticClass(),
					NAME_None);
			if (!Blueprint)
			{
				OutError =
					TEXT("Could not create continuation probe Blueprint.");
				return false;
			}
			Out.Asset.Reset(Blueprint);

			UEdGraph* Graph =
				FBlueprintEditorUtils::FindEventGraph(
					Blueprint);
			if (!Graph)
			{
				OutError =
					TEXT("Continuation probe has no event graph.");
				return false;
			}

			UK2Node_CustomEvent* Entry =
				NewObject<UK2Node_CustomEvent>(Graph);
			Entry->CustomFunctionName = EntryPointName;
			AddNode(*Graph, *Entry);

			const UFunction* Factory =
				USeinMoveToProxy::StaticClass()
					->FindFunctionByName(
						GET_FUNCTION_NAME_CHECKED(
							USeinMoveToProxy,
							SeinMoveTo));
			if (!Factory)
			{
				OutError =
					TEXT("Move To factory is missing.");
				return false;
			}
			UK2Node_AsyncAction* MoveTo =
				NewObject<UK2Node_AsyncAction>(Graph);
			MoveTo->InitializeProxyFromFunction(Factory);
			AddNode(*Graph, *MoveTo);
			UK2Node_AsyncAction* SecondMoveTo = nullptr;
			if (bSequential)
			{
				SecondMoveTo =
					NewObject<UK2Node_AsyncAction>(Graph);
				SecondMoveTo->InitializeProxyFromFunction(
					Factory);
				AddNode(*Graph, *SecondMoveTo);
			}

			const UEdGraphSchema_K2* Schema =
				GetDefault<UEdGraphSchema_K2>();
			const FFixedVector ProbeDestination(
				FFixedPoint::FromInt(300),
				FFixedPoint::FromInt(120),
				FFixedPoint::Zero);
			FString DestinationText;
			FFixedVector::StaticStruct()->ExportText(
				DestinationText,
				&ProbeDestination,
				nullptr,
				Blueprint,
				PPF_None,
				Blueprint);
			Schema->TrySetDefaultValue(
				*MoveTo->FindPinChecked(
					TEXT("Destination")),
				DestinationText,
				false);
			if (SecondMoveTo)
			{
				const FFixedVector SecondDestination(
					FFixedPoint::FromInt(610),
					FFixedPoint::FromInt(240),
					FFixedPoint::Zero);
				FString SecondDestinationText;
				FFixedVector::StaticStruct()->ExportText(
					SecondDestinationText,
					&SecondDestination,
					nullptr,
					Blueprint,
					PPF_None,
					Blueprint);
				Schema->TrySetDefaultValue(
					*SecondMoveTo->FindPinChecked(
						TEXT("Destination")),
					SecondDestinationText,
					false);
			}
			if (!Schema->TryCreateConnection(
				Entry->FindPinChecked(
					UEdGraphSchema_K2::PN_Then),
				MoveTo->FindPinChecked(
					UEdGraphSchema_K2::PN_Execute)))
			{
				OutError =
					TEXT("Could not connect the Move To execution input.");
				return false;
			}

			for (int32 Index = 0;
				Index < RouteCount; ++Index)
			{
				const FRoute& Route = Routes()[Index];
				const UFunction* Recorder =
					USeinMoveToContinuationEditorTestAbility::
						StaticClass()->FindFunctionByName(
							Route.Recorder);
				if (!Recorder)
				{
					OutError =
						TEXT("Continuation recorder is missing.");
					return false;
				}

				UK2Node_CallFunction* Call =
					NewObject<UK2Node_CallFunction>(Graph);
				Call->SetFromFunction(Recorder);
				AddNode(*Graph, *Call);
				if (!Schema->TryCreateConnection(
						MoveTo->FindPinChecked(
							Route.Channel),
						Call->FindPinChecked(
							UEdGraphSchema_K2::PN_Execute))
					|| !Schema->TryCreateConnection(
						MoveTo->FindPinChecked(
							TEXT("Result")),
						Call->FindPinChecked(
							TEXT("Result"))))
				{
					OutError =
						TEXT("Could not connect a Move To output route.");
					return false;
				}
				if (SecondMoveTo
					&& Route.Channel
						== GET_MEMBER_NAME_CHECKED(
							USeinMoveToProxy,
							OnCancelled)
					&& !Schema->TryCreateConnection(
						Call->FindPinChecked(
							UEdGraphSchema_K2::PN_Then),
						SecondMoveTo->FindPinChecked(
							UEdGraphSchema_K2::PN_Execute)))
				{
					OutError =
						TEXT("Could not launch the second Move To after cancellation.");
					return false;
				}
			}

			if (SecondMoveTo)
			{
				for (int32 Index = 0;
					Index < RouteCount; ++Index)
				{
					const FRoute& Route = Routes()[Index];
					const UFunction* Recorder =
						USeinMoveToContinuationEditorTestAbility::
							StaticClass()->FindFunctionByName(
								Route.Recorder);
					UK2Node_CallFunction* Call =
						NewObject<UK2Node_CallFunction>(Graph);
					Call->SetFromFunction(Recorder);
					AddNode(*Graph, *Call);
					if (!Schema->TryCreateConnection(
							SecondMoveTo->FindPinChecked(
								Route.Channel),
							Call->FindPinChecked(
								UEdGraphSchema_K2::PN_Execute))
						|| !Schema->TryCreateConnection(
							SecondMoveTo->FindPinChecked(
								TEXT("Result")),
							Call->FindPinChecked(
								TEXT("Result"))))
					{
						OutError =
							TEXT("Could not connect a second Move To output route.");
						return false;
					}
				}
			}

			FBlueprintEditorUtils::
				MarkBlueprintAsStructurallyModified(
					Blueprint);
			FCompilerResultsLog CompileLog;
			CompileLog.bSilentMode = true;
			FKismetEditorUtilities::CompileBlueprint(
				Blueprint,
				EBlueprintCompileOptions::
					SkipGarbageCollection,
				&CompileLog);
			if (CompileLog.NumErrors != 0
				|| !Blueprint->GeneratedClass)
			{
				TArray<FString> Diagnostics;
				constexpr int32 MaxDiagnostics = 8;
				for (const TSharedRef<FTokenizedMessage>& Message :
					CompileLog.Messages)
				{
					if (Diagnostics.Num() == MaxDiagnostics)
					{
						break;
					}
					Diagnostics.Add(
						Message->ToText().ToString());
				}
				OutError = FString::Printf(
					TEXT("Continuation probe compile produced %d errors.%s%s"),
					CompileLog.NumErrors,
					Diagnostics.IsEmpty() ? TEXT("") : TEXT(" "),
					*FString::Join(Diagnostics, TEXT(" | ")));
				return false;
			}

			Out.Class = Blueprint->GeneratedClass;
			Out.PoolClassAdmission =
				FSeinPoolObjectCodecRegistry::
					RegisterExplicitLocalClassForTests(
						Out.Class,
						&OutError);
			if (!Out.PoolClassAdmission.IsValid())
			{
				return false;
			}
			const int32 MoveNodeCount =
				SecondMoveTo ? 2 : 1;
			TMap<FGuid, TMap<FName, FName>> CallbacksByNode;
			for (TFieldIterator<UFunction> It(
					Out.Class,
					EFieldIteratorFlags::IncludeSuper);
				It;
				++It)
			{
				const UFunction* Callback = *It;
				if (!Callback
					|| Callback->GetOuterUClass() != Out.Class)
				{
					continue;
				}
				for (int32 Index = 0;
					Index < RouteCount; ++Index)
				{
					const FName Channel =
						Routes()[Index].Channel;
					const FString Prefix =
						Channel.ToString() + TEXT("_");
					const FString CallbackName =
						Callback->GetName();
					if (!CallbackName.StartsWith(
							Prefix,
							ESearchCase::CaseSensitive))
					{
						continue;
					}
					FGuid NodeGuid;
					const FString GuidText =
						CallbackName.RightChop(Prefix.Len());
					if (!FGuid::ParseExact(
							GuidText,
							EGuidFormats::Digits,
							NodeGuid)
						|| !NodeGuid.IsValid()
						|| NodeGuid.ToString(
							EGuidFormats::Digits)
							!= GuidText)
					{
						continue;
					}
					TMap<FName, FName>& CallbackSet =
						CallbacksByNode.FindOrAdd(NodeGuid);
					if (CallbackSet.Contains(Channel))
					{
						OutError =
							TEXT("Generated callback route was duplicated.");
						return false;
					}
					CallbackSet.Add(
						Channel, Callback->GetFName());
					break;
				}
			}
			if (CallbacksByNode.Num() != MoveNodeCount)
			{
				OutError =
					TEXT("Generated async callback groups were incomplete.");
				return false;
			}
			TArray<FGuid> NodeGuids;
			CallbacksByNode.GetKeys(NodeGuids);
			NodeGuids.Sort([](const FGuid& A, const FGuid& B)
			{
				return A.ToString(EGuidFormats::Digits)
					< B.ToString(EGuidFormats::Digits);
			});
			for (const FGuid& NodeGuid : NodeGuids)
			{
				const TMap<FName, FName>& CallbackSet =
					CallbacksByNode.FindChecked(NodeGuid);
				if (CallbackSet.Num() != RouteCount)
				{
					OutError =
						TEXT("Generated async callback group is incomplete.");
					return false;
				}
				Out.CallbackSets.Add(CallbackSet);
			}
			return true;
		}

		struct FFixture
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			USeinLatentActionManager* Manager = nullptr;
			USeinMoveToContinuationEditorTestAbility*
				Ability = nullptr;
			USeinMoveToAction* Action = nullptr;
			USeinMoveToProxy* Proxy = nullptr;
			FSeinEntityHandle Entity;
			int32 AbilityID = INDEX_NONE;

			~FFixture()
			{
				USeinMoveToContinuationEditorTestMovement::
					Reset();
			}

			bool Initialize(
				const FCompiledBlueprint& Blueprint,
				bool bInvokeMoveTo = true,
				bool bEnableRepath = false,
				bool bEnableEscapeRecovery = false)
			{
				USeinMoveToContinuationEditorTestMovement::
					Reset();
				USeinMoveToContinuationEditorTestMovement::
					bAdvanceInitialWaypointOnTick =
						bEnableEscapeRecovery;
				World = Spawner.GetWorld().GetSubsystem<
					USeinWorldSubsystem>();
				if (!World || !Blueprint.Class)
				{
					return false;
				}

				FString Error;
				if (!SeinTestMatchBootstrap::Materialize(
					*World,
					[&]()
					{
						Entity =
							World->SpawnAbstractEntity(
								FFixedTransform(),
								FSeinPlayerID::Neutral());
						FSeinMovementComponent Movement;
						Movement.MovementClass =
							FSoftClassPath(
								USeinMoveToContinuationEditorTestMovement::
									StaticClass()
										->GetPathName());
						World->AddComponent(
							Entity, Movement);
						if (bEnableRepath
							|| bEnableEscapeRecovery)
						{
							FSeinNavigationComponent Navigation;
							if (bEnableEscapeRecovery)
							{
								Navigation.FallbackFootprintRadius =
									FFixedPoint::FromInt(25);
								Navigation.NavLayerMask = 0x04;
								Navigation.RepathMode =
									ESeinRepathMode::Interval;
								Navigation.RepathInterval =
									FFixedPoint::FromInt(100);
							}
							else
							{
								Navigation.RepathMode =
									ESeinRepathMode::Interval;
								Navigation.RepathInterval =
									FFixedPoint::One
									/ FFixedPoint::FromInt(4);
							}
							World->AddComponent(
								Entity, Navigation);
						}
						World->AddComponent(
							Entity,
							FSeinAbilityComponent());
						AbilityID =
							USeinAbilityBPFL::
								SeinGrantAbility(
									World,
									Entity,
									Blueprint.Class);
					},
					FSeinMatchSettings(),
					0,
					TEXT("SeinARTS.MoveToContinuation"),
					&Error)
					|| !Entity.IsValid()
					|| AbilityID == INDEX_NONE
					|| !SeinTestMatchBootstrap::Start(
						*World, &Error))
				{
					return false;
				}

				Manager = World->LatentActionManager;
				Ability = Cast<
					USeinMoveToContinuationEditorTestAbility>(
						World->GetAbilityInstance(
							AbilityID));
				if (!Manager || !Ability)
				{
					return false;
				}

				{
					auto SimScope =
						FSeinSimContextTestAccess::Enter(
							*World);
					if (!Ability->ActivateAbility(
						FSeinEntityHandle::Invalid(),
						FFixedVector::ZeroVector))
					{
						return false;
					}
					if (bInvokeMoveTo)
					{
						UFunction* Entry =
							Blueprint.Class
								->FindFunctionByName(
									EntryPointName);
						if (!Entry)
						{
							return false;
						}
						Ability->ProcessEvent(
							Entry, nullptr);
					}
				}

				if (!bInvokeMoveTo)
				{
					return Manager
						->GetActiveActionCount() == 0;
				}
				if (Manager->GetActiveActionCount() != 1)
				{
					return false;
				}
				Action = Cast<USeinMoveToAction>(
					Manager->GetActiveActions()[0]);
				Proxy = Action
					? FMoveToActionContinuationTestAccess::
						GetObserver(*Action)
					: nullptr;
				return Action && Proxy
					&& Action->OwningAbility == Ability
					&& Action->OwnerEntity == Entity
					&& FMoveToActionContinuationTestAccess::
						GetRunningAction(*Proxy)
						== Action;
			}

			void Tick()
			{
				auto SimScope =
					FSeinSimContextTestAccess::Enter(
						*World);
				Manager->TickAll(
					FFixedPoint::FromInt(1)
						/ FFixedPoint::FromInt(10),
					*World);
			}

			void RefreshRestoredPointers()
			{
				Manager = World->LatentActionManager;
				Ability = Cast<
					USeinMoveToContinuationEditorTestAbility>(
						World->GetAbilityInstance(
							AbilityID));
				Action = Manager
					&& Manager->GetActiveActionCount() == 1
					? Cast<USeinMoveToAction>(
						Manager->GetActiveActions()[0])
					: nullptr;
				Proxy = Action
					? FMoveToActionContinuationTestAccess::
						GetObserver(*Action)
					: nullptr;
			}
		};

		struct FRestoredFixture
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			USeinLatentActionManager* Manager = nullptr;
			USeinMoveToContinuationEditorTestAbility*
				Ability = nullptr;
			USeinMoveToAction* Action = nullptr;
			USeinMoveToProxy* Proxy = nullptr;

			bool Restore(
				const FSeinWorldSnapshot& Snapshot,
				int32 AbilityID)
			{
				World = Spawner.GetWorld().GetSubsystem<
					USeinWorldSubsystem>();
				if (!World
					|| !SeinTestSnapshotRestore::RestoreTrusted(
						*World, Snapshot))
				{
					return false;
				}
				Manager = World->LatentActionManager;
				Ability = Cast<
					USeinMoveToContinuationEditorTestAbility>(
						World->GetAbilityInstance(
							AbilityID));
				Action = Manager
					&& Manager->GetActiveActionCount() == 1
					? Cast<USeinMoveToAction>(
						Manager->GetActiveActions()[0])
					: nullptr;
				Proxy = Action
					? FMoveToActionContinuationTestAccess::
						GetObserver(*Action)
					: nullptr;
				return Ability && Action && Proxy;
			}

			void Tick()
			{
				auto SimScope =
					FSeinSimContextTestAccess::Enter(
						*World);
				Manager->TickAll(
					FFixedPoint::FromInt(1)
						/ FFixedPoint::FromInt(10),
					*World);
			}
		};

		bool CanonicalRootsMatch(
			USeinWorldSubsystem& A,
			USeinWorldSubsystem& B,
			FString& OutError)
		{
			FGuid RootA;
			FGuid RootB;
			if (!A.ComputeCanonicalStateRoot(
					RootA, OutError)
				|| !B.ComputeCanonicalStateRoot(
					RootB, OutError))
			{
				return false;
			}
			if (RootA == RootB)
			{
				return true;
			}

			FSeinWorldSnapshot SnapshotA;
			FSeinWorldSnapshot SnapshotB;
			A.CaptureSnapshot(SnapshotA);
			B.CaptureSnapshot(SnapshotB);

			const bool bAbilityStateMatches =
				SnapshotA.AbilityPoolRecords.Num()
					== SnapshotB.AbilityPoolRecords.Num()
				&& SnapshotA.AbilityPoolRecords.Num() == 1
				&& SnapshotA.AbilityPoolRecords[0].StateBytes
					== SnapshotB.AbilityPoolRecords[0].StateBytes;
			const bool bLatentStateMatches =
				SnapshotA.LatentActionRecords.Num()
					== SnapshotB.LatentActionRecords.Num()
				&& SnapshotA.LatentActionSequenceDigest
					== SnapshotB.LatentActionSequenceDigest;
			const bool bNativeStateMatches =
				SnapshotA.NativeCanonicalStateRecords.Num()
					== SnapshotB.NativeCanonicalStateRecords.Num();
			bool bNativePayloadsMatch = bNativeStateMatches;
			if (bNativePayloadsMatch)
			{
				for (int32 Index = 0;
					Index
						< SnapshotA.NativeCanonicalStateRecords.Num();
					++Index)
				{
					const FSeinCanonicalStateContributorRecord& RecordA =
						SnapshotA.NativeCanonicalStateRecords[Index];
					const FSeinCanonicalStateContributorRecord& RecordB =
						SnapshotB.NativeCanonicalStateRecords[Index];
					if (RecordA.Key != RecordB.Key
						|| RecordA.LeafDigest != RecordB.LeafDigest)
					{
						bNativePayloadsMatch = false;
						break;
					}
				}
			}
			bool bComponentsMatch =
				SnapshotA.ComponentStorageBlobs.Num()
					== SnapshotB.ComponentStorageBlobs.Num();
			if (bComponentsMatch)
			{
				for (const TPair<
						FString,
						FSeinSnapshotComponentStorageBlob>& Pair :
					SnapshotA.ComponentStorageBlobs)
				{
					const FSeinSnapshotComponentStorageBlob* Other =
						SnapshotB.ComponentStorageBlobs.Find(Pair.Key);
					if (!Other
						|| Pair.Value.EntryCount != Other->EntryCount
						|| Pair.Value.Bytes != Other->Bytes)
					{
						bComponentsMatch = false;
						break;
					}
				}
			}

			OutError = FString::Printf(
				TEXT("Canonical roots diverged: A=%s B=%s ability=%d latent=%d native=%d components=%d."),
				*RootA.ToString(),
				*RootB.ToString(),
				bAbilityStateMatches,
				bLatentStateMatches,
				bNativePayloadsMatch,
				bComponentsMatch);
			UE_LOG(LogTemp, Warning, TEXT("%s"), *OutError);
			return false;
		}

		bool AllRoutesBound(
			const USeinMoveToProxy& Proxy)
		{
			return Proxy.OnCompleted.IsBound()
				&& Proxy.OnFailed.IsBound()
				&& Proxy.OnWaypointReached.IsBound()
				&& Proxy.OnCancelled.IsBound()
				&& Proxy.OnPartialPath.IsBound()
				&& Proxy.OnPathRecomputed.IsBound();
		}

		bool RoutesExactlyMatch(
			const USeinMoveToProxy& Proxy,
			const USeinAbility& Ability,
			const TMap<FName, FName>& CallbackSet)
		{
			if (CallbackSet.Num() != RouteCount)
			{
				return false;
			}
			for (int32 Index = 0; Index < RouteCount; ++Index)
			{
				const FRoute& Route = Routes()[Index];
				const FName* Function =
					CallbackSet.Find(Route.Channel);
				const FSeinMoveToDelegate& Delegate =
					Proxy.*(Route.Delegate);
				const TArray<UObject*> Targets =
					Delegate.GetAllObjectsEvenIfUnreachable();
				if (!Function
					|| Targets.Num() != 1
					|| Targets[0] != &Ability
					|| !Delegate.Contains(
						&Ability, *Function))
				{
					return false;
				}
			}
			return true;
		}

		const TMap<FName, FName>* FindExactCallbackSet(
			const USeinMoveToProxy& Proxy,
			const USeinAbility& Ability,
			const TArray<TMap<FName, FName>>& CallbackSets)
		{
			const TMap<FName, FName>* Match = nullptr;
			for (const TMap<FName, FName>& Candidate :
				CallbackSets)
			{
				if (!RoutesExactlyMatch(
					Proxy, Ability, Candidate))
				{
					continue;
				}
				if (Match)
				{
					return nullptr;
				}
				Match = &Candidate;
			}
			return Match;
		}

		bool IsGeneratedTempName(
			const FString& Name,
			const FString& Base)
		{
			if (Name == Base)
			{
				return true;
			}
			const FString Prefix = Base + TEXT("_");
			if (!Name.StartsWith(
					Prefix, ESearchCase::CaseSensitive))
			{
				return false;
			}
			const FString Suffix = Name.RightChop(Prefix.Len());
			if (Suffix.IsEmpty() || Suffix[0] == TEXT('0'))
			{
				return false;
			}
			for (const TCHAR Character : Suffix)
			{
				if (!FChar::IsDigit(Character))
				{
					return false;
				}
			}
			return true;
		}

		bool IsGeneratedDelegateTempName(
			const FString& Name)
		{
			return IsGeneratedTempName(
				Name,
				TEXT("K2Node_CreateDelegate_OutputDelegate"));
		}

		bool GeneratedDelegateTempsMatch(
			USeinAbility& Ability,
			const TArray<TMap<FName, FName>>& CallbackSets,
			bool bExpectBound)
		{
			TSet<FName> RemainingCallbacks;
			for (const TMap<FName, FName>& CallbackSet :
				CallbackSets)
			{
				for (const TPair<FName, FName>& Pair :
					CallbackSet)
				{
					RemainingCallbacks.Add(Pair.Value);
				}
			}
			const int32 ExpectedTempCount =
				CallbackSets.Num() * RouteCount;

			int32 TempCount = 0;
			for (const UClass* Class = Ability.GetClass();
				Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* FrameClass =
					Cast<UBlueprintGeneratedClass>(Class);
				UFunction* Function = FrameClass
					? FrameClass->UberGraphFunction
					: nullptr;
				uint8* Frame = Function
					? FrameClass->GetPersistentUberGraphFrame(
						&Ability, Function)
					: nullptr;
				if (!Frame)
				{
					continue;
				}
				for (FProperty* Property = Function->PropertyLink;
					Property;
					Property = Property->PropertyLinkNext)
				{
					const FDelegateProperty* DelegateProperty =
						CastField<FDelegateProperty>(Property);
					if (!DelegateProperty
						|| Property->ArrayDim != 1
						|| !IsGeneratedDelegateTempName(
							Property->GetName()))
					{
						continue;
					}
					++TempCount;
					const FScriptDelegate Value =
						DelegateProperty
							->GetPropertyValue_InContainer(
								Frame);
					if (bExpectBound)
					{
						if (Value
								.GetUObjectEvenIfUnreachable()
								!= &Ability
							|| RemainingCallbacks.Remove(
								Value.GetFunctionName()) != 1)
						{
							return false;
						}
					}
					else if (Value
							.GetUObjectEvenIfUnreachable()
						|| !Value.GetFunctionName().IsNone())
					{
						return false;
					}
				}
			}
			return TempCount == ExpectedTempCount
				&& (!bExpectBound
					|| RemainingCallbacks.IsEmpty());
		}

		bool GeneratedValidityTempsMatch(
			USeinAbility& Ability,
			int32 ExpectedCount,
			bool bExpectedValue)
		{
			int32 Count = 0;
			for (const UClass* Class = Ability.GetClass();
				Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* FrameClass =
					Cast<UBlueprintGeneratedClass>(Class);
				UFunction* Function = FrameClass
					? FrameClass->UberGraphFunction
					: nullptr;
				uint8* Frame = Function
					? FrameClass->GetPersistentUberGraphFrame(
						&Ability, Function)
					: nullptr;
				if (!Frame)
				{
					continue;
				}
				for (FProperty* Property =
						Function->PropertyLink;
					Property;
					Property = Property->PropertyLinkNext)
				{
					const FBoolProperty* BoolProperty =
						CastField<FBoolProperty>(Property);
					if (BoolProperty
						&& IsGeneratedTempName(
							Property->GetName(),
							TEXT("CallFunc_IsValid_ReturnValue")))
					{
						++Count;
						if (BoolProperty
							->GetPropertyValue_InContainer(
								Frame)
							!= bExpectedValue)
						{
							return false;
						}
					}
				}
			}
			return Count == ExpectedCount;
		}

		bool GeneratedMoveResultTempsMatch(
			USeinAbility& Ability,
			int32 ExpectedStorageCount,
			int32 ExpectedNonDefaultStorage,
			int32 ExpectedCallbackCount,
			int32 ExpectedNonDefaultCallbacks,
			FString* OutDiagnostic = nullptr)
		{
			int32 StorageCount = 0;
			int32 NonDefaultStorage = 0;
			int32 CallbackCount = 0;
			int32 NonDefaultCallbacks = 0;
			int32 UnknownCount = 0;
			TArray<FString> Properties;
			for (const UClass* Class = Ability.GetClass();
				Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* FrameClass =
					Cast<UBlueprintGeneratedClass>(Class);
				UFunction* Function = FrameClass
					? FrameClass->UberGraphFunction
					: nullptr;
				if (!Function)
				{
					continue;
				}
				uint8* Frame =
					FrameClass->GetPersistentUberGraphFrame(
						&Ability, Function);
				FStructOnScope DefaultFrame(Function);
				if (!Frame || !DefaultFrame.IsValid())
				{
					continue;
				}
				for (FProperty* Property =
						Function->PropertyLink;
					Property;
					Property = Property->PropertyLinkNext)
				{
					const FStructProperty* StructProperty =
						CastField<FStructProperty>(Property);
					if (!StructProperty
						|| StructProperty->Struct
							!= FSeinMoveToResult::StaticStruct())
					{
						continue;
					}
					const bool bIsNonDefault =
						!Property->Identical_InContainer(
							Frame,
							DefaultFrame.GetStructMemory());
					Properties.Add(FString::Printf(
						TEXT("%s.%s%s"),
						*FrameClass->GetPathName(),
						*Property->GetName(),
						bIsNonDefault
							? TEXT("=non-default")
							: TEXT("=default")));
					if (!IsGeneratedTempName(
							Property->GetName(),
							TEXT("Temp_struct_Variable")))
					{
						if (IsGeneratedTempName(
								Property->GetName(),
								TEXT("K2Node_CustomEvent_Result")))
						{
							++CallbackCount;
							if (bIsNonDefault)
							{
								++NonDefaultCallbacks;
							}
						}
						else
						{
							++UnknownCount;
						}
						continue;
					}
					++StorageCount;
					if (bIsNonDefault)
					{
						++NonDefaultStorage;
					}
				}
			}
			if (OutDiagnostic)
			{
				*OutDiagnostic = FString::Printf(
					TEXT("Expected result storage %d/%d non-default and callback params %d/%d non-default; found %d/%d and %d/%d, with %d unknown. All FSeinMoveToResult frame properties: %s"),
					ExpectedStorageCount,
					ExpectedNonDefaultStorage,
					ExpectedCallbackCount,
					ExpectedNonDefaultCallbacks,
					StorageCount,
					NonDefaultStorage,
					CallbackCount,
					NonDefaultCallbacks,
					UnknownCount,
					*FString::Join(Properties, TEXT(", ")));
			}
			return StorageCount == ExpectedStorageCount
				&& NonDefaultStorage
					== ExpectedNonDefaultStorage
				&& CallbackCount == ExpectedCallbackCount
				&& NonDefaultCallbacks
					== ExpectedNonDefaultCallbacks
				&& UnknownCount == 0;
		}

		bool ReplacePersistentFrameObject(
			USeinAbility& Ability,
			const UObject& Expected,
			UObject& Replacement)
		{
			for (const UClass* Class = Ability.GetClass();
				Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* FrameClass =
					Cast<UBlueprintGeneratedClass>(Class);
				UFunction* Function = FrameClass
					? FrameClass->UberGraphFunction
					: nullptr;
				uint8* Frame = Function
					? FrameClass->GetPersistentUberGraphFrame(
						&Ability, Function)
					: nullptr;
				if (!Frame)
				{
					continue;
				}

				for (FProperty* Property =
						Function->PropertyLink;
					Property;
					Property = Property->PropertyLinkNext)
				{
					FObjectPropertyBase* ObjectProperty =
						Property->HasAnyPropertyFlags(CPF_Parm)
							? nullptr
							: CastField<FObjectPropertyBase>(
								Property);
					if (!ObjectProperty
						|| !Replacement.IsA(
							ObjectProperty->PropertyClass))
					{
						continue;
					}
					for (int32 ArrayIndex = 0;
						ArrayIndex < Property->ArrayDim;
						++ArrayIndex)
					{
						if (ObjectProperty
								->GetObjectPropertyValue_InContainer(
									Frame, ArrayIndex)
							!= &Expected)
						{
							continue;
						}
						ObjectProperty
							->SetObjectPropertyValue_InContainer(
								Frame, &Replacement,
								ArrayIndex);
						return true;
					}
				}
			}
			return false;
		}

		bool ReplacePersistentFrameDelegate(
			USeinAbility& Ability,
			FName ExpectedFunction,
			FName ReplacementFunction)
		{
			for (const UClass* Class = Ability.GetClass();
				Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* FrameClass =
					Cast<UBlueprintGeneratedClass>(Class);
				UFunction* Function = FrameClass
					? FrameClass->UberGraphFunction
					: nullptr;
				uint8* Frame = Function
					? FrameClass->GetPersistentUberGraphFrame(
						&Ability, Function)
					: nullptr;
				if (!Frame)
				{
					continue;
				}
				for (FProperty* Property =
						Function->PropertyLink;
					Property;
					Property = Property->PropertyLinkNext)
				{
					FDelegateProperty* DelegateProperty =
						Property->HasAnyPropertyFlags(CPF_Parm)
							? nullptr
							: CastField<FDelegateProperty>(
								Property);
					if (!DelegateProperty
						|| Property->ArrayDim != 1
						|| !IsGeneratedDelegateTempName(
							Property->GetName()))
					{
						continue;
					}
					const FScriptDelegate Existing =
						DelegateProperty
							->GetPropertyValue_InContainer(
								Frame);
					if (Existing
							.GetUObjectEvenIfUnreachable()
							!= &Ability
						|| Existing.GetFunctionName()
							!= ExpectedFunction)
					{
						continue;
					}
					FScriptDelegate Replacement;
					Replacement.BindUFunction(
						&Ability, ReplacementFunction);
					DelegateProperty
						->SetPropertyValue_InContainer(
							Frame, Replacement);
					return true;
				}
			}
			return false;
		}
	}

	TEST(MoveToContinuationRestoresExactMidMoveBlueprintLifecycle,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));

		FFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(Blueprint)));
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				PlanCount));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				BeginCount));
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->PartialPathCount));
		ASSERT_THAT(IsTrue(
			GeneratedValidityTempsMatch(
				*Fixture.Ability, 1, true)));
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Fixture.Ability,
				Blueprint.CallbackSets,
				true)));

		USeinMovementSubsystem* MovementSubsystem =
			Fixture.World->GetWorld()->GetSubsystem<
				USeinMovementSubsystem>();
		USeinMoveToContinuationEditorTestMovement*
			Movement = MovementSubsystem
				? Cast<
					USeinMoveToContinuationEditorTestMovement>(
						MovementSubsystem
							->FindMovementInstance(
								Fixture.Entity))
				: nullptr;
		ASSERT_THAT(IsNotNull(Movement));
		Movement->PersistentValue =
			FFixedPoint::FromInt(731);

		USeinMoveToAction* Expected =
			FMoveToActionContinuationTestAccess::
				CloneMappedFields(
					*Fixture.Action,
					*GetTransientPackage());
		ASSERT_THAT(IsNotNull(Expected));
		const int64 ExpectedActionID =
			Fixture.Action->GetActionID();
		const int64 ExpectedActivationID =
			Fixture.Action->GetAbilityActivationID();
		const int64 ExpectedNextActionID =
			Fixture.Manager->GetNextActionID();
		USeinMoveToProxy* OldProxy = Fixture.Proxy;

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1, Snapshot.LatentActionRecords.Num()));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsSilentlyDetached(*OldProxy)));
		ASSERT_THAT(AreEqual(
			0,
			USeinMoveToContinuationEditorTestMovement::
				EndCount));

		Fixture.RefreshRestoredPointers();
		ASSERT_THAT(IsNotNull(Fixture.Ability));
		ASSERT_THAT(IsNotNull(Fixture.Action));
		ASSERT_THAT(IsNotNull(Fixture.Proxy));
		ASSERT_THAT(IsTrue(
			Fixture.Action != Expected));
		ASSERT_THAT(AreEqual(
			ExpectedActionID,
			Fixture.Action->GetActionID()));
		ASSERT_THAT(AreEqual(
			ExpectedActivationID,
			Fixture.Action
				->GetAbilityActivationID()));
		ASSERT_THAT(AreEqual(
			ExpectedNextActionID,
			Fixture.Manager->GetNextActionID()));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				MappedFieldsEqual(
					*Expected,
					*Fixture.Action,
					Error)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				DiagnosticsWereReset(
					*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			AllRoutesBound(*Fixture.Proxy)));
		ASSERT_THAT(IsTrue(
			GeneratedValidityTempsMatch(
				*Fixture.Ability, 1, false)));
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Fixture.Ability,
				Blueprint.CallbackSets,
				false)));
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->PartialPathCount));

		Movement = Cast<
			USeinMoveToContinuationEditorTestMovement>(
				MovementSubsystem->FindMovementInstance(
					Fixture.Entity));
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsTrue(
			Movement->PersistentValue
				== FFixedPoint::FromInt(731)));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				PlanCount));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				BeginCount));

		TWeakObjectPtr<USeinMoveToProxy> RestoredProxy(
			Fixture.Proxy);
		CollectGarbage(RF_NoFlags);
		ASSERT_THAT(IsTrue(RestoredProxy.IsValid()));
		ASSERT_THAT(AreEqual(
			Fixture.Action,
			FMoveToActionContinuationTestAccess::
				GetRunningAction(
					*RestoredProxy.Get())));

		Fixture.Proxy->NotifyPathRecomputed();
		ASSERT_THAT(AreEqual(
			1,
			Fixture.Ability->
				PathRecomputedCount));
		USeinMoveToContinuationEditorTestMovement::
			bAdvanceWaypoint = true;
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->WaypointCount));
		ASSERT_THAT(AreEqual(
			0,
			Fixture.Ability->LastResult.WaypointIndex));
		ASSERT_THAT(AreEqual(
			3,
			Fixture.Ability->LastResult.TotalWaypoints));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				PlanCount));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				BeginCount));

		USeinMoveToContinuationEditorTestMovement::
			bFinishMove = true;
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->CompletedCount));
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::
				EndCount));
		ASSERT_THAT(AreEqual(
			0,
			Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsSilentlyDetached(*Fixture.Proxy)));
	}

	TEST(MoveToContinuationMatchesFreshWorldAcrossFutureTicks,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));

		FFixture Source;
		ASSERT_THAT(IsTrue(Source.Initialize(Blueprint)));
		Source.Tick();

		USeinMovementSubsystem* SourceMovementSubsystem =
			Source.World->GetWorld()->GetSubsystem<
				USeinMovementSubsystem>();
		USeinMoveToContinuationEditorTestMovement*
			SourceMovement = SourceMovementSubsystem
				? Cast<
					USeinMoveToContinuationEditorTestMovement>(
						SourceMovementSubsystem
							->FindMovementInstance(
								Source.Entity))
				: nullptr;
		ASSERT_THAT(IsNotNull(SourceMovement));
		SourceMovement->PersistentValue =
			FFixedPoint::FromInt(90210);
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Source.Ability,
				Blueprint.CallbackSets,
				true)));

		FSeinWorldSnapshot Snapshot;
		Source.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));

		FRestoredFixture Destination;
		ASSERT_THAT(IsTrue(Destination.Restore(
			Snapshot, Source.AbilityID)));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error)));
		ASSERT_THAT(IsTrue(
			AllRoutesBound(*Destination.Proxy)));
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Destination.Ability,
				Blueprint.CallbackSets,
				false)));

		TWeakObjectPtr<USeinMoveToProxy> RestoredProxy(
			Destination.Proxy);
		CollectGarbage(RF_NoFlags);
		ASSERT_THAT(IsTrue(RestoredProxy.IsValid()));

		Source.Proxy->NotifyPathRecomputed();
		Destination.Proxy->NotifyPathRecomputed();
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error)));

		USeinMoveToContinuationEditorTestMovement::
			bAdvanceWaypoint = true;
		Source.Tick();
		USeinMoveToContinuationEditorTestMovement::
			bAdvanceWaypoint = true;
		Destination.Tick();
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error),
			Error));
		ASSERT_THAT(AreEqual(
			Source.Ability->WaypointCount,
			Destination.Ability->WaypointCount));

		USeinMoveToContinuationEditorTestMovement::
			bFinishMove = true;
		Source.Tick();
		Destination.Tick();
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error)));
		ASSERT_THAT(AreEqual(
			0, Source.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			0,
			Destination.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			Source.Ability->CompletedCount,
			Destination.Ability->CompletedCount));
	}

	TEST(MoveToContinuationCrossesRealRepathBoundaryExactly,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		ASSERT_THAT(IsTrue(
			CompileBlueprint(Blueprint, Error), Error));

		FFixture Source;
		ASSERT_THAT(IsTrue(
			Source.Initialize(Blueprint, true, true)));
		Source.Tick();
		Source.Tick();
		ASSERT_THAT(AreEqual(
			1,
			USeinMoveToContinuationEditorTestMovement::PlanCount));
		ASSERT_THAT(AreEqual(
			0, Source.Ability->PathRecomputedCount));
		const FFixedPoint SourceElapsed =
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Source.Action);
		ASSERT_THAT(IsTrue(SourceElapsed > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			SourceElapsed
				< FFixedPoint::One / FFixedPoint::FromInt(4)));

		FSeinWorldSnapshot Snapshot;
		Source.World->CaptureSnapshot(Snapshot);
		FRestoredFixture Destination;
		ASSERT_THAT(IsTrue(Destination.Restore(
			Snapshot, Source.AbilityID)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Destination.Action) == SourceElapsed));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error), Error));

		USeinMoveToContinuationEditorTestMovement::PlanCount = 0;
		Source.Tick();
		Destination.Tick();

		ASSERT_THAT(AreEqual(
			2,
			USeinMoveToContinuationEditorTestMovement::PlanCount));
		ASSERT_THAT(AreEqual(
			1, Source.Ability->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, Destination.Ability->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			2, Source.Ability->PartialPathCount));
		ASSERT_THAT(AreEqual(
			2, Destination.Ability->PartialPathCount));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::MappedFieldsEqual(
				*Source.Action,
				*Destination.Action,
				Error), Error));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *Destination.World, Error), Error));
	}

	TEST(MoveToContinuationRestoresEscapeRecoveryBoundariesExactly,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FScopedEscapeNavigation ScopedNavigation;
		FCompiledBlueprint Blueprint;
		FString Error;
		ASSERT_THAT(IsTrue(
			CompileBlueprint(Blueprint, Error), Error));

		FFixture Source;
		ASSERT_THAT(IsTrue(
			Source.Initialize(Blueprint, true, false, true)));
		Source.Tick();
		Source.Tick();
		Source.Tick();
		Source.Tick();
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				HasStageOneFired(*Source.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsForceRepathPending(*Source.Action)));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::
				IsEscapeMode(*Source.Action)));

		FSeinWorldSnapshot StageOneSnapshot;
		Source.World->CaptureSnapshot(StageOneSnapshot);
		FRestoredFixture StageOneRestore;
		ASSERT_THAT(IsTrue(StageOneRestore.Restore(
			StageOneSnapshot, Source.AbilityID)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::MappedFieldsEqual(
				*Source.Action, *StageOneRestore.Action, Error),
			Error));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *StageOneRestore.World, Error), Error));

		for (int32 TickIndex = 0; TickIndex < 3; ++TickIndex)
		{
			Source.Tick();
			StageOneRestore.Tick();
			ASSERT_THAT(IsTrue(CanonicalRootsMatch(
				*Source.World, *StageOneRestore.World, Error), Error));
		}
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsEscapeMode(*Source.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsEscapeMode(*StageOneRestore.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::MappedFieldsEqual(
				*Source.Action, *StageOneRestore.Action, Error),
			Error));

		FSeinWorldSnapshot EscapeSnapshot;
		Source.World->CaptureSnapshot(EscapeSnapshot);
		FRestoredFixture EscapeRestore;
		ASSERT_THAT(IsTrue(EscapeRestore.Restore(
			EscapeSnapshot, Source.AbilityID)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::MappedFieldsEqual(
				*Source.Action, *EscapeRestore.Action, Error),
			Error));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Source.World, *EscapeRestore.World, Error), Error));

		for (int32 TickIndex = 0; TickIndex < 7; ++TickIndex)
		{
			Source.Tick();
			EscapeRestore.Tick();
			ASSERT_THAT(IsTrue(CanonicalRootsMatch(
				*Source.World, *EscapeRestore.World, Error), Error));
		}
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::
				IsEscapeMode(*Source.Action)));
		ASSERT_THAT(AreEqual(
			1,
			FMoveToActionContinuationTestAccess::
				GetEscapeAttempts(*Source.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::MappedFieldsEqual(
				*Source.Action, *EscapeRestore.Action, Error),
			Error));
	}

	TEST(MoveToContinuationSurvivesSequentialBlueprintNodes,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		ASSERT_THAT(IsTrue(
			CompileBlueprint(Blueprint, Error, true),
			Error));

		FFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(Blueprint)));
		USeinMoveToProxy* FirstProxy = Fixture.Proxy;
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(
					*Fixture.World);
			Fixture.Manager->CancelActionsForAbility(
				Fixture.Ability);
			Fixture.Manager->CleanupCompleted();
		}
		Fixture.RefreshRestoredPointers();
		ASSERT_THAT(AreEqual(
			1, Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(IsNotNull(Fixture.Action));
		ASSERT_THAT(IsNotNull(Fixture.Proxy));
		ASSERT_THAT(IsTrue(Fixture.Proxy != FirstProxy));
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->CancelledCount));
		ASSERT_THAT(IsTrue(
			Fixture.Ability->LastResult.FailureReason
				== ESeinMoveFailureReason::Cancelled));
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Fixture.Ability,
				Blueprint.CallbackSets,
				true)));
		ASSERT_THAT(IsTrue(
			GeneratedValidityTempsMatch(
				*Fixture.Ability, 2, true)));
		ASSERT_THAT(IsTrue(
			GeneratedMoveResultTempsMatch(
				*Fixture.Ability, 2, 1, 12, 1, &Error),
			Error));
		const TMap<FName, FName>* ActiveCallbackSet =
			FindExactCallbackSet(
				*Fixture.Proxy,
				*Fixture.Ability,
				Blueprint.CallbackSets);
		ASSERT_THAT(IsNotNull(ActiveCallbackSet));
		const TMap<FName, FName> SecondNodeCallbacks =
			*ActiveCallbackSet;
		const TMap<FName, FName>* InactiveCallbackSet =
			nullptr;
		for (const TMap<FName, FName>& Candidate :
			Blueprint.CallbackSets)
		{
			if (&Candidate != ActiveCallbackSet)
			{
				InactiveCallbackSet = &Candidate;
				break;
			}
		}
		ASSERT_THAT(IsNotNull(InactiveCallbackSet));

		FGuid RootBeforeRestore;
		ASSERT_THAT(IsTrue(
			Fixture.World->ComputeCanonicalStateRoot(
				RootBeforeRestore, Error),
			Error));
		USeinMoveToProxy* SecondProxy = Fixture.Proxy;
		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1, Snapshot.LatentActionRecords.Num()));
		const FName* InactiveCompleted =
			InactiveCallbackSet->Find(
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnCompleted));
		ASSERT_THAT(IsNotNull(InactiveCompleted));
		FSeinWorldSnapshot MixedRoutes = Snapshot;
		ASSERT_THAT(IsTrue(
			ReplaceMoveToCompletedRouteForTest(
				MixedRoutes.LatentActionRecords[0],
				InactiveCompleted->ToString(),
				Error)));
		ASSERT_THAT(IsTrue(
			FSeinLatentActionCodecRegistry::
				RecomputeRecordDigestsForTests(
					MixedRoutes.NextLatentActionID,
					MixedRoutes.NextAbilityActivationID,
					MixedRoutes.LatentActionRecords,
					MixedRoutes
						.LatentActionSequenceDigest,
					Error)));
		Assert.ExpectError(TEXT(
			"RestoreSnapshot: latent continuation state failed staging"));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, MixedRoutes)));
		ASSERT_THAT(AreEqual(
			SecondProxy,
			FMoveToActionContinuationTestAccess::
				GetObserver(*Fixture.Action)));
		ASSERT_THAT(AreEqual(
			Fixture.Action,
			Fixture.Manager->GetActiveActions()[0]
				.Get()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Snapshot)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				IsSilentlyDetached(*SecondProxy)));

		Fixture.RefreshRestoredPointers();
		ASSERT_THAT(IsNotNull(Fixture.Action));
		ASSERT_THAT(IsNotNull(Fixture.Proxy));
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->CancelledCount));
		ASSERT_THAT(IsTrue(
			GeneratedDelegateTempsMatch(
				*Fixture.Ability,
				Blueprint.CallbackSets,
				false)));
		ASSERT_THAT(IsTrue(
			GeneratedValidityTempsMatch(
				*Fixture.Ability, 2, false)));
		ASSERT_THAT(IsTrue(
			GeneratedMoveResultTempsMatch(
				*Fixture.Ability, 2, 0, 12, 0, &Error),
			Error));
		ASSERT_THAT(IsTrue(
			RoutesExactlyMatch(
				*Fixture.Proxy,
				*Fixture.Ability,
				SecondNodeCallbacks)));
		FGuid RootAfterRestore;
		ASSERT_THAT(IsTrue(
			Fixture.World->ComputeCanonicalStateRoot(
				RootAfterRestore, Error),
			Error));
		ASSERT_THAT(IsTrue(
			RootAfterRestore == RootBeforeRestore));

		FSeinWorldSnapshot Recheckpoint;
		Fixture.World->CaptureSnapshot(Recheckpoint);
		ASSERT_THAT(AreEqual(
			1, Recheckpoint.LatentActionRecords.Num()));
		FRestoredFixture Recheckpointed;
		ASSERT_THAT(IsTrue(
			Recheckpointed.Restore(
				Recheckpoint, Fixture.AbilityID)));
		ASSERT_THAT(IsTrue(
			RoutesExactlyMatch(
				*Recheckpointed.Proxy,
				*Recheckpointed.Ability,
				SecondNodeCallbacks)));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Fixture.World,
			*Recheckpointed.World,
			Error)));

		USeinMoveToContinuationEditorTestMovement::
			bFinishMove = true;
		Fixture.Tick();
		Recheckpointed.Tick();
		ASSERT_THAT(AreEqual(
			0, Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			0,
			Recheckpointed.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			1, Fixture.Ability->CompletedCount));
		ASSERT_THAT(AreEqual(
			Fixture.Ability->CompletedCount,
			Recheckpointed.Ability->CompletedCount));
		ASSERT_THAT(IsTrue(CanonicalRootsMatch(
			*Fixture.World,
			*Recheckpointed.World,
			Error)));
	}

	TEST(MoveToContinuationRejectsForeignBlueprintRoute,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));
		FFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(Blueprint)));

		TStrongObjectPtr<
			USeinMoveToContinuationEditorTestObserver>
			Foreign(NewObject<
				USeinMoveToContinuationEditorTestObserver>(
					Fixture.World));
		ASSERT_THAT(IsNotNull(Foreign.Get()));
		Fixture.Proxy->OnCompleted.AddDynamic(
			Foreign.Get(),
			&USeinMoveToContinuationEditorTestObserver::RecordForeign);

		Assert.ExpectError(TEXT(
			"must have exactly one binding to its owning ability"));
		FSeinWorldSnapshot Refused;
		Fixture.World->CaptureSnapshot(Refused);
		ASSERT_THAT(AreEqual(
			0, Refused.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1,
			Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			Fixture.Action,
			Fixture.Manager->GetActiveActions()[0]
				.Get()));
	}

	TEST(MoveToContinuationRejectsUnsafeBlueprintFrameState,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));
		FFixture Fixture;
		ASSERT_THAT(IsTrue(
			Fixture.Initialize(Blueprint)));

		USeinMoveToProxy* ForeignProxy =
			NewObject<USeinMoveToProxy>(
				Fixture.World);
		ASSERT_THAT(IsNotNull(ForeignProxy));
		ASSERT_THAT(IsTrue(
			ReplacePersistentFrameObject(
				*Fixture.Ability,
				*Fixture.Proxy,
				*ForeignProxy)));

		Assert.ExpectError(TEXT(
			"does not retain the active proxy"));
		FSeinWorldSnapshot Refused;
		Fixture.World->CaptureSnapshot(Refused);
		ASSERT_THAT(AreEqual(
			0, Refused.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1,
			Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(AreEqual(
			Fixture.Action,
				Fixture.Manager->GetActiveActions()[0]
					.Get()));
	}

	TEST(MoveToContinuationRejectsUnsafeBlueprintDelegateResidue,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		ASSERT_THAT(IsTrue(
			CompileBlueprint(Blueprint, Error),
			Error));
		FFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(Blueprint)));
		const TMap<FName, FName>* CallbackSet =
			FindExactCallbackSet(
				*Fixture.Proxy,
				*Fixture.Ability,
				Blueprint.CallbackSets);
		ASSERT_THAT(IsNotNull(CallbackSet));
		const FName* CompletedFunction =
			CallbackSet->Find(
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnCompleted));
		ASSERT_THAT(IsNotNull(CompletedFunction));
		ASSERT_THAT(IsTrue(
			ReplacePersistentFrameDelegate(
				*Fixture.Ability,
				*CompletedFunction,
				GET_FUNCTION_NAME_CHECKED(
					USeinMoveToContinuationEditorTestAbility,
					RecordCompleted))));

		Assert.ExpectError(TEXT(
			"cannot checkpoint non-default Blueprint frame value"));
		FSeinWorldSnapshot Refused;
		Fixture.World->CaptureSnapshot(Refused);
		ASSERT_THAT(AreEqual(
			0, Refused.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			Fixture.Action,
			Fixture.Manager->GetActiveActions()[0]
				.Get()));
	}

	TEST(MoveToContinuationRejectsDirectNativeConstruction,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));
		FFixture Fixture;
		ASSERT_THAT(IsTrue(
			Fixture.Initialize(
				Blueprint, false)));

		USeinMoveToAction* Direct =
			NewObject<USeinMoveToAction>(
				Fixture.World);
		ASSERT_THAT(IsNotNull(Direct));
		Direct->OwningAbility = Fixture.Ability;
		Direct->OwnerEntity = Fixture.Entity;
		Direct->Initialize(
			FFixedVector::ZeroVector);
		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(
					*Fixture.World);
			ASSERT_THAT(IsTrue(
				Fixture.Manager->RegisterAction(
					Direct)));
		}

		Assert.ExpectError(TEXT(
			"requires the exact active action/proxy/ability ownership graph"));
		FSeinWorldSnapshot Refused;
		Fixture.World->CaptureSnapshot(Refused);
		ASSERT_THAT(AreEqual(
			0, Refused.SnapshotVersion));
		ASSERT_THAT(AreEqual(
			1,
			Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToContinuationMalformedPayloadsFailAtomically,
		"SeinARTS.Editor.Snapshot.Movement")
	{
		using namespace MoveToContinuationEditor;
		FCompiledBlueprint Blueprint;
		FString Error;
		const bool bCompiled =
			CompileBlueprint(Blueprint, Error);
		ASSERT_THAT(IsTrue(bCompiled, Error));
		FFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(Blueprint)));
		Fixture.Tick();

		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		ASSERT_THAT(AreEqual(
			1, Valid.LatentActionRecords.Num()));
		USeinMoveToAction* LiveAction =
			Fixture.Action;
		USeinMoveToProxy* LiveProxy =
			Fixture.Proxy;
		USeinAbility* LiveAbility =
			Fixture.Ability;
		const int32 PlanCount =
			USeinMoveToContinuationEditorTestMovement::
				PlanCount;
		const int32 BeginCount =
			USeinMoveToContinuationEditorTestMovement::
				BeginCount;

		const EMoveToContinuationMutation Mutations[] = {
			EMoveToContinuationMutation::
				InvalidWaypointCursor,
			EMoveToContinuationMutation::
				ResolvedWithoutMovement,
			EMoveToContinuationMutation::
				NonstandardCompletedRoute,
			EMoveToContinuationMutation::
				EscapeCounterOutsideBound,
		};
		for (const EMoveToContinuationMutation Mutation :
			Mutations)
		{
			FSeinWorldSnapshot Corrupted = Valid;
			ASSERT_THAT(IsTrue(
				MutateMoveToContinuationForTest(
					Corrupted.LatentActionRecords[0],
					Mutation, Error)));
			ASSERT_THAT(IsTrue(
				FSeinLatentActionCodecRegistry::
					RecomputeRecordDigestsForTests(
						Corrupted.NextLatentActionID,
						Corrupted.NextAbilityActivationID,
						Corrupted.LatentActionRecords,
						Corrupted
							.LatentActionSequenceDigest,
						Error)));

			Assert.ExpectError(TEXT(
				"RestoreSnapshot: latent continuation state failed staging"));
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*Fixture.World, Corrupted)));
			ASSERT_THAT(AreEqual(
				LiveAction,
				Fixture.Manager->GetActiveActions()[0]
					.Get()));
			ASSERT_THAT(AreEqual(
				LiveProxy,
				FMoveToActionContinuationTestAccess::
					GetObserver(*LiveAction)));
			ASSERT_THAT(AreEqual(
				LiveAbility,
				Fixture.World->GetAbilityInstance(
					Fixture.AbilityID)));
			ASSERT_THAT(AreEqual(
				PlanCount,
				USeinMoveToContinuationEditorTestMovement::
					PlanCount));
			ASSERT_THAT(AreEqual(
				BeginCount,
				USeinMoveToContinuationEditorTestMovement::
					BeginCount));
			ASSERT_THAT(AreEqual(
				0,
				USeinMoveToContinuationEditorTestMovement::
					EndCount));
		}
	}
}
