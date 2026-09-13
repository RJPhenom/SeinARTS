/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionMigrationCommandlet.cpp
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Records the existing construction graphs for a narrow, reviewable migration.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Commandlets/SeinConstructionMigrationCommandlet.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "FileHelpers.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "WidgetBlueprint.h"
#include "Authoring/SeinEntityComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ActorComponents/SeinConstructionRenderComponent.h"
#include "Widget/SeinEntityWidget.h"
#include "Widget/SeinEntityWidgetComponent.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"


#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableGet.h"
#include "K2Node_SwitchEnum.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lib/SeinConstructionBPFL.h"
#include "Abilities/SeinAbility.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "GameFramework/Actor.h"
#include "Util/SeinSimulationContentManifestBuilder.h"

namespace ConstructionMigration
{
	// This one-time migration only accepts the exact three graphs audited before implementation.
	// Any designer edit requires a new audit instead of discarding unknown graph behavior.
	bool Preflight()
	{
		const TCHAR* Names[] = {TEXT("SA_Place_Barracks"), TEXT("SA_Place_Factory"), TEXT("SA_Build")};
		const TCHAR* Hashes[] = {TEXT("898867027AE1A7D9DF1371966616F4FC9A0307F7"),
			TEXT("9B7FDFDDC62420680FFFD5BE40DCB19CE55B423A"), TEXT("DD510B1EFDBD9E7C33944B6A0556E2430C4F3F53")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			const FString Filename = FPackageName::LongPackageNameToFilename(
				FString(TEXT("/SeinARTSFramework/Demo/Blueprints/")) + Names[Index], FPackageName::GetAssetPackageExtension());
			TArray<uint8> Bytes; FSHAHash Hash;
			if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) return false;
			FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
			if (!Hash.ToString().Equals(Hashes[Index], ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Error, TEXT("Migration requires a fresh audit: %s differs from the reviewed asset."), Names[Index]);
				return false;
			}
		}
		return true;
	}
	const UEdGraphSchema_K2* Schema() { return GetDefault<UEdGraphSchema_K2>(); }
	void Add(UEdGraph* Graph, UK2Node* Node, int32 X, int32 Y)
	{
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins();
		Node->NodePosX = X; Node->NodePosY = Y;
	}
	UK2Node_CallFunction* Call(UEdGraph* Graph, UClass* Class, FName Function, int32 X, int32 Y)
	{
		auto* Node = NewObject<UK2Node_CallFunction>(Graph);
		Node->SetFromFunction(Class->FindFunctionByName(Function));
		Add(Graph, Node, X, Y);
		return Node;
	}
	bool Link(UEdGraphPin* A, UEdGraphPin* B) { return A && B && Schema()->TryCreateConnection(A, B); }
	void Bypass(UK2Node_CallFunction* Node)
	{
		auto* In = Node->FindPin(UEdGraphSchema_K2::PN_Execute);
		auto* Out = Node->FindPin(UEdGraphSchema_K2::PN_Then);
		const auto Inputs = In ? In->LinkedTo : TArray<UEdGraphPin*>();
		const auto Outputs = Out ? Out->LinkedTo : TArray<UEdGraphPin*>();
		Node->BreakAllNodeLinks();
		for (auto* A : Inputs) for (auto* B : Outputs) Link(A, B);
		Node->DestroyNode();
	}
	bool Placement(UBlueprint* BP)
	{
		TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
		int32 SpawnCount = 0;
		for (UEdGraph* Graph : Graphs)
		{
			const auto Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes)
			{
				auto* Fn = Cast<UK2Node_CallFunction>(Node);
				if (!Fn) continue;
				const FName Name = Fn->FunctionReference.GetMemberName();
				if (Name == TEXT("SeinSpawnConstructionSite")) { ++SpawnCount; continue; }
				if (Name == TEXT("SeinSpawnEntity"))
				{
					Fn->SetFromFunction(USeinConstructionBPFL::StaticClass()->FindFunctionByName(TEXT("SeinSpawnConstructionSite")));
					Fn->ReconstructNode();
					if (auto* Self = Fn->FindPin(UEdGraphSchema_K2::PN_Self)) Self->DefaultObject = USeinConstructionBPFL::StaticClass()->GetDefaultObject();
					Schema()->TrySetDefaultValue(*Fn->FindPinChecked(TEXT("InitialState")), TEXT("Queued"));
					++SpawnCount;
				}
				else if (Name == TEXT("SeinGrantTag"))
				{
					auto* Tag = Fn->FindPin(TEXT("Tag"));
					if (Tag && Tag->LinkedTo.IsEmpty() && Tag->DefaultValue == TEXT("(TagName=\"SeinARTS.State.UnderConstruction\")"))
						Bypass(Fn);
				}
			}
		}
		return SpawnCount == 1;
	}
	bool Worker(UBlueprint* BP)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP);
		if (!Graph) return false;
		UK2Node_Event* Activate = nullptr;
		UK2Node_Event* Tick = nullptr;
		UK2Node_CallFunction* Advance = nullptr;
		UK2Node_VariableGet* Target = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Event = Cast<UK2Node_Event>(Node))
			{
				if (Event->EventReference.GetMemberName() == TEXT("OnActivate")) Activate = Event;
				if (Event->EventReference.GetMemberName() == TEXT("OnTick")) Tick = Event;
			}
			if (auto* Fn = Cast<UK2Node_CallFunction>(Node))
			{
				if (Fn->FunctionReference.GetMemberName() == TEXT("SeinAdvanceConstruction")) return true;
				if (Fn->FunctionReference.GetMemberName() == TEXT("SeinAddConstructionProgress")) Advance = Fn;
			}
			if (auto* Get = Cast<UK2Node_VariableGet>(Node))
				if (Get->VariableReference.GetMemberName() == TEXT("TargetEntity")) Target = Get;
		}
		if (!Activate || !Tick || !Advance || !Target || !Activate->FindPinChecked(TEXT("then"))->LinkedTo.IsEmpty()) return false;
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = FSeinConstructionHandle::StaticStruct();
		if (!FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Construction"), Type)) return false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		// The audited worker graph has no other connected gameplay behavior. Preserve its event and target nodes.
		const auto Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node == Activate || Node == Tick || Node == Target || Cast<UK2Node_Event>(Node)) continue;
			Node->BreakAllNodeLinks(); Node->DestroyNode();
		}
		Target->NodePosX = 0; Target->NodePosY = 160;
		Activate->NodePosX = 0; Activate->NodePosY = 0;
		Tick->NodePosX = 0; Tick->NodePosY = 520;
		auto* Status = Call(Graph, USeinConstructionBPFL::StaticClass(), TEXT("SeinGetConstructionStatus"), 240, 160);
		if (!Link(Target->FindPinChecked(TEXT("TargetEntity")), Status->FindPinChecked(TEXT("Entity")))) return false;
		Schema()->SplitPin(Status->FindPinChecked(TEXT("ReturnValue")), false);
		auto* Set = NewObject<UK2Node_VariableSet>(Graph);
		Set->VariableReference.SetSelfMember(TEXT("Construction")); Add(Graph, Set, 520, 0);
		auto* Get = NewObject<UK2Node_VariableGet>(Graph);
		Get->VariableReference.SetSelfMember(TEXT("Construction")); Add(Graph, Get, 560, 370);
		auto* Start = Call(Graph, USeinConstructionBPFL::StaticClass(), TEXT("SeinStartConstruction"), 800, 0);
		Advance = Call(Graph, USeinConstructionBPFL::StaticClass(), TEXT("SeinAdvanceConstruction"), 800, 520);
		auto* Complete = Call(Graph, USeinConstructionBPFL::StaticClass(), TEXT("SeinCompleteConstruction"), 1450, 160);
		auto* End = Call(Graph, USeinAbility::StaticClass(), TEXT("EndAbility"), 1780, 160);
		auto* Cancel = Call(Graph, USeinAbility::StaticClass(), TEXT("CancelAbility"), 1780, 700);
		bool OK = Link(Activate->FindPinChecked(TEXT("then")), Set->FindPinChecked(TEXT("execute")))
			&& Link(Status->FindPinChecked(TEXT("ReturnValue_Construction")), Set->FindPinChecked(TEXT("Construction")))
			&& Link(Set->FindPinChecked(TEXT("then")), Start->FindPinChecked(TEXT("execute")))
			&& Link(Tick->FindPinChecked(TEXT("then")), Advance->FindPinChecked(TEXT("execute")))
			&& Link(Tick->FindPinChecked(TEXT("DeltaTime")), Advance->FindPinChecked(TEXT("Amount")))
			&& Link(Complete->FindPinChecked(TEXT("then")), End->FindPinChecked(TEXT("execute")));
		for (auto* Node : {Start, Advance, Complete})
			OK &= Link(Get->FindPinChecked(TEXT("Construction")), Node->FindPinChecked(TEXT("Construction")));
		int32 Y = 0;
		for (auto* Node : {Start, Advance})
		{
			auto* Switch = NewObject<UK2Node_SwitchEnum>(Graph);
			Switch->SetEnum(StaticEnum<ESeinConstructionResult>());
			Add(Graph, Switch, 1120, Y); Y = 520;
			OK &= Link(Node->FindPinChecked(TEXT("then")), Switch->FindPinChecked(TEXT("execute")));
			OK &= Link(Node->FindPinChecked(TEXT("ReturnValue")), Switch->FindPinChecked(TEXT("Selection")));
			for (UEdGraphPin* Pin : Switch->Pins)
			{
				if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				const int64 Value = StaticEnum<ESeinConstructionResult>()->GetValueByName(Pin->PinName);
				if (Value == static_cast<int64>(ESeinConstructionResult::ReadyToComplete))
					OK &= Link(Pin, Complete->FindPinChecked(TEXT("execute")));
				else if (Value == static_cast<int64>(ESeinConstructionResult::AlreadyComplete))
					OK &= Link(Pin, End->FindPinChecked(TEXT("execute")));
				else if (Value != static_cast<int64>(ESeinConstructionResult::Succeeded)
					&& Value != static_cast<int64>(ESeinConstructionResult::Unchanged)
					&& Value != static_cast<int64>(ESeinConstructionResult::InvalidState))
					OK &= Link(Pin, Cancel->FindPinChecked(TEXT("execute")));
			}
		}
		return OK;
	}
	bool Save(UBlueprint* BP, const FString& BackupDirectory)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error) return false;
		UPackage* Package = BP->GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Backup = BackupDirectory / FPaths::GetCleanFilename(Filename);
		if (IFileManager::Get().Copy(*Backup, *Filename, false, false) != COPY_OK) return false;
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.Error = GError;
		return UPackage::SavePackage(Package, BP, *Filename, Args);
	}
	bool Apply()
	{
		if (!Preflight()) return false;
		const FString BackupDirectory = FPaths::ProjectSavedDir() / TEXT("Construction/Backups") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
		IFileManager::Get().MakeDirectory(*BackupDirectory, true);
		for (const TCHAR* Name : {TEXT("SA_Place_Barracks"), TEXT("SA_Place_Factory"), TEXT("SA_Build")})
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *(FString(TEXT("/SeinARTSFramework/Demo/Blueprints/")) + Name));
			if (!BP || !(FString(Name) == TEXT("SA_Build") ? Worker(BP) : Placement(BP)) || !Save(BP, BackupDirectory))
			{
				UE_LOG(LogTemp, Error, TEXT("Construction migration stopped at %s; backups: %s"), Name, *BackupDirectory);
				return false;
			}
			UE_LOG(LogTemp, Display, TEXT("Migrated %s; backup: %s"), Name, *BackupDirectory);
		}
		return true;
	}
}


namespace ConstructionRefactor
{
	bool Apply()
	{
		const FString Root = TEXT("/SeinARTSFramework/Demo/Blueprints/");
		auto* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("UI/WBP_ConstructionProgressBar")));
		if (!WidgetBP || !WidgetBP->WidgetTree || !WidgetBP->WidgetTree->FindWidget(TEXT("ProgressBar_61"))) return false;
		TArray<UBlueprint*> Buildings;
		TArray<UBlueprint*> Previews;
		for (const TCHAR* Name : {TEXT("Barracks"), TEXT("Factory")})
		{
			auto* Building = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("SU_") + Name));
			auto* Preview = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("BP_") + Name + TEXT("ConstructionPreview")));
			if (!Building || !Building->SimpleConstructionScript || !Preview || !Preview->SimpleConstructionScript) return false;
			if (!Building->SimpleConstructionScript->FindSCSNode(TEXT("SM_Tent"))) return false;
			Buildings.Add(Building); Previews.Add(Preview);
		}
		// Back up the current working assets, including edits made since the first migration.
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("ConstructionRefactor/Backups") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
		IFileManager::Get().MakeDirectory(*Backup, true);
		TArray<UBlueprint*> Assets = Buildings; Assets.Add(WidgetBP);
		for (auto* BP : Assets)
		{
			const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			if (IFileManager::Get().Copy(*(Backup / FPaths::GetCleanFilename(Filename)), *Filename, false, false) != COPY_OK) return false;
		}
		// Preserve the widget layout; replace its percent/visibility side-effect binding with the work example policy.
		WidgetBP->Bindings.RemoveAll([](const FDelegateEditorBinding& Binding)
		{
			return Binding.FunctionName == TEXT("GetPercent");
		});
		const auto Graphs = WidgetBP->FunctionGraphs;
		for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == TEXT("GetPercent")) FBlueprintEditorUtils::RemoveGraph(WidgetBP, Graph);
		FBlueprintEditorUtils::RemoveMemberVariable(WidgetBP, TEXT("Entity"));
		WidgetBP->ParentClass = USeinConstructionWorkProgressWidget::StaticClass();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
		if (WidgetBP->Status == BS_Error) return false;
		CastChecked<USeinConstructionWorkProgressWidget>(WidgetBP->GeneratedClass->GetDefaultObject())->ProgressBarName = TEXT("ProgressBar_61");

		for (int32 Index = 0; Index < Buildings.Num(); ++Index)
		{
			auto* BP = Buildings[Index];
			auto* SCS = BP->SimpleConstructionScript.Get();
			auto* RootNode = SCS->FindSCSNode(TEXT("DefaultSceneRoot"));
			auto* FinishedNode = SCS->FindSCSNode(TEXT("SM_Tent"));
			if (!RootNode || !FinishedNode) return false;
			USeinConstructionComponent* Construction = nullptr;
			USeinConstructionRenderComponent* Renderer = nullptr;
			for (auto* Node : SCS->GetAllNodes())
			{
				if (auto* C = Cast<USeinConstructionComponent>(Node->ComponentTemplate)) Construction = C;
				if (auto* R = Cast<USeinConstructionRenderComponent>(Node->ComponentTemplate)) Renderer = R;
			}
			if (!Construction) return false;
			Construction->Construction.RequiredWork = Construction->Construction.TimeToCompletion;

			if (!Renderer)
			{
				auto* Node = SCS->CreateNode(USeinConstructionRenderComponent::StaticClass(), TEXT("SeinConstructionRender"));
				SCS->AddNode(Node);
				Renderer = CastChecked<USeinConstructionRenderComponent>(Node->ComponentTemplate);
			}
			auto* VisualNode = SCS->FindSCSNode(TEXT("ConstructionVisual"));
			if (!VisualNode)
			{
				auto* SourceNode = Previews[Index]->SimpleConstructionScript->FindSCSNode(TEXT("SM_Tent"));
				auto* Source = SourceNode ? Cast<UStaticMeshComponent>(SourceNode->ComponentTemplate) : nullptr;
				if (!Source) return false;
				VisualNode = SCS->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("ConstructionVisual"));
				auto* Visual = CastChecked<UStaticMeshComponent>(VisualNode->ComponentTemplate);
				Visual->SetStaticMesh(Source->GetStaticMesh());
				for (int32 Material = 0; Material < Source->GetNumMaterials(); ++Material) Visual->SetMaterial(Material, Source->GetMaterial(Material));
				Visual->SetRelativeTransform(Source->GetRelativeTransform());
				Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Visual->SetVisibility(false);
				RootNode->AddChildNode(VisualNode);
			}
			FSeinPresentationGroup Finished; Finished.Name = TEXT("Finished"); Finished.bInitiallyVisible = true;
			FComponentReference FinishedReference; FinishedReference.ComponentProperty = FinishedNode->GetVariableName();
			Finished.Components.Add(FinishedReference);
			FSeinPresentationGroup UnderConstruction; UnderConstruction.Name = TEXT("Construction");
			FComponentReference ConstructionReference; ConstructionReference.ComponentProperty = VisualNode->GetVariableName();
			UnderConstruction.Components.Add(ConstructionReference);
			Renderer->Groups = {Finished, UnderConstruction};
			Renderer->PlacementVisualType = ESeinConstructionPlacementVisualType::None;
			Renderer->PlacementBlueprint = nullptr;
			Renderer->PlacementStaticMesh = nullptr;
			Renderer->PlacementSkeletalMesh = nullptr;
			Renderer->GroundStampDecal = nullptr;
			auto* WidgetNode = SCS->FindSCSNode(TEXT("ConstructionProgress"));
			if (!WidgetNode)
			{
				WidgetNode = SCS->CreateNode(USeinEntityWidgetComponent::StaticClass(), TEXT("ConstructionProgress"));
				RootNode->AddChildNode(WidgetNode);
			}
			auto* Component = CastChecked<USeinEntityWidgetComponent>(WidgetNode->ComponentTemplate);
			Component->SetWidgetClass(TSubclassOf<UUserWidget>(WidgetBP->GeneratedClass.Get()));
			Component->SetWidgetSpace(EWidgetSpace::Screen);
			Component->SetDrawSize(FVector2D(200, 16));
			FVector Location(0, 0, 200);
			if (auto* HealthNode = SCS->FindSCSNode(TEXT("BP_HealthBarWidgetComponent")))
				if (auto* Health = Cast<USceneComponent>(HealthNode->ComponentTemplate)) Location = Health->GetRelativeLocation() + FVector(0, 0, 30);
			Component->SetRelativeLocation(Location);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (BP->Status == BS_Error) return false;
			TArray<const USeinEntityBridgeComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(TSubclassOf<AActor>(BP->GeneratedClass.Get()), Bridges);
			for (const auto* Bridge : Bridges) const_cast<USeinEntityBridgeComponent*>(Bridge)->BakeAuthoredDataComponents(false);
		}
		for (auto* BP : Assets)
		{
			// Already backed up before any edits. Save directly after successful compile and authoring bake.
			const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.Error = GError;
			if (!UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args)) return false;
			UE_LOG(LogTemp, Display, TEXT("Refactored %s; backup %s"), *BP->GetName(), *Backup);
		}
		return true;
	}
}

USeinConstructionMigrationCommandlet::USeinConstructionMigrationCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 USeinConstructionMigrationCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("Refactor")) && !ConstructionRefactor::Apply()) return 1;
	if (FParse::Param(*Params, TEXT("Apply")) && !ConstructionMigration::Apply()) return 1;
	if (FParse::Param(*Params, TEXT("Manifest")))
	{
		FSeinSimulationContentManifestBuildResult Result; FString Error;
		if (!FSeinSimulationContentManifestBuilder::GenerateConfiguredManifest(Result, Error))
		{ UE_LOG(LogTemp, Error, TEXT("Manifest: %s"), *Error); return 1; }
	}
	if (FParse::Param(*Params, TEXT("VerifyMaps")))
	{
		for (const TCHAR* Path : {TEXT("/Game/Sandbox"), TEXT("/SeinARTSFramework/Demo/LVL_DemoSkirmish")})
		{
			auto* World = UEditorLoadingAndSavingUtils::LoadMap(Path);
			if (!World || !World->PersistentLevel) return 1;
			for (const AActor* Actor : World->PersistentLevel->Actors)
			{
				if (!Actor) continue;
				const auto* Construction = Actor->FindComponentByClass<USeinConstructionComponent>();
				if (!Construction) continue;
				const auto* Bridge = Actor->FindComponentByClass<USeinEntityBridgeComponent>();
				bool bFound = false;
				if (Bridge) for (const auto& Data : Bridge->ComponentData)
				{
					if (const auto* Baked = Data.GetPtr<FSeinConstructionPayload>())
					{
						if (Baked->RequiredWork != Construction->Construction.RequiredWork) return 1;
						bFound = true;
					}
				}
				if (!bFound && Construction->bInjectionEnabled) return 1;
				UE_LOG(LogTemp, Display, TEXT("Verified merged construction %s RequiredWork=%s"), *Actor->GetPathName(), *Construction->Construction.RequiredWork.ToString());
			}
		}
	}
	FString Report;
	for (const TCHAR* Name : {TEXT("SA_Place_Barracks"), TEXT("SA_Place_Factory"), TEXT("SA_Build"),
		TEXT("BP_BarracksConstructionPreview"), TEXT("BP_FactoryConstructionPreview"), TEXT("UI/WBP_ConstructionProgressBar"), TEXT("SU_Barracks"), TEXT("SU_Factory")})
	{
		const FString Path = FString(TEXT("/SeinARTSFramework/Demo/Blueprints/")) + Name;
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
		if (!BP) { UE_LOG(LogTemp, Error, TEXT("Cannot load %s"), *Path); return 1; }
		if (FParse::Param(*Params, TEXT("Verify")))
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (BP->Status == BS_Error) { UE_LOG(LogTemp, Error, TEXT("Construction verification failed: %s"), *Path); return 1; }
		}
		Report += Path + TEXT("\n");
		Report += TEXT("PARENT ") + GetPathNameSafe(BP->ParentClass) + TEXT("\n");
		if (BP->SimpleConstructionScript)
			for (const auto* Node : BP->SimpleConstructionScript->GetAllNodes())
				Report += FString::Printf(TEXT("COMPONENT %s %s\n"), *Node->GetVariableName().ToString(), *GetPathNameSafe(Node->ComponentClass));
		if (const auto* WBP = Cast<UWidgetBlueprint>(BP))
		{
			TArray<UWidget*> Widgets; WBP->WidgetTree->GetAllWidgets(Widgets);
			for (const auto* Widget : Widgets) Report += TEXT("WIDGET ") + Widget->GetName() + TEXT("\n");
		}
		if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			TArray<const USeinEntityBridgeComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(TSubclassOf<AActor>(BP->GeneratedClass.Get()), Bridges);
			for (const auto* Bridge : Bridges)
			{
				Report += TEXT("BASE TAGS ") + Bridge->BaseTags.ToStringSimple() + TEXT("\n");
				for (const auto& Data : Bridge->ComponentData)
				{
					if (const auto* Work = Data.GetPtr<FSeinConstructionPayload>()) Report += FString::Printf(TEXT("WORK REQUIREMENT %s\n"), *Work->RequiredWork.ToString());
					if (const auto* Construction = Data.GetPtr<FSeinConstructionPayload>()) Report += FString::Printf(TEXT("START QUEUED %d\n"), Construction->bQueueConstructionOnSpawn);
				}
			}
		}
		TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			Report += TEXT("GRAPH ") + Graph->GetName() + TEXT("\n");
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				Report += FString::Printf(TEXT("NODE %s %s %s\n"), *Node->GetName(),
					*Node->GetClass()->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
					Report += TEXT("FUNCTION ") + Call->FunctionReference.GetMemberName().ToString() + TEXT("\n");
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					Report += FString::Printf(TEXT(" PIN %s %s DEFAULT %s OBJECT %s LINKS "),
						Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"), *Pin->PinName.ToString(),
						*Pin->DefaultValue, *GetPathNameSafe(Pin->DefaultObject));
					for (const UEdGraphPin* Link : Pin->LinkedTo)
						Report += Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString() + TEXT(" ");
					Report += TEXT("\n");
				}
			}
		}
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Construction/asset-audit.txt");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Report, *Path)) return 1;
	UE_LOG(LogTemp, Display, TEXT("Construction audit: %s"), *Path);
	return 0;
}
