#include "CQTest.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "UObject/ObjectKey.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Containers/Ticker.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Graph/K2Node_SeinAbilityInputs.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Components/ActorComponent.h"
#include "Lib/SeinAbilityInputBPFL.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "UObject/Script.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	UBlueprint* MakeBlueprint(UClass* Parent)
	{
		return FKismetEditorUtilities::CreateBlueprint(Parent, GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("ActivationInputTest")),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	}
	void AddInput(UBlueprint* BP, FName Name, FEdGraphPinType Type, const FString& Default = FString())
	{
		FBlueprintEditorUtils::AddMemberVariable(BP, Name, Type, Default);
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, TEXT("SeinExposeOnActivate"), TEXT("true"));
	}
	UK2Node_SeinAbilityInputs* Builder(UBlueprint* Caller, UClass* Ability)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Caller, TEXT("BuildInputs"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph(Caller, Graph, true, static_cast<UFunction*>(nullptr));
		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes) if (auto* E = Cast<UK2Node_FunctionEntry>(Node)) Entry = E;
		UK2Node_FunctionResult* Result = NewObject<UK2Node_FunctionResult>(Graph);
		Graph->AddNode(Result); Result->CreateNewGuid(); Result->AllocateDefaultPins();
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Struct; Type.PinSubCategoryObject = FSeinAbilityActivationInputs::StaticStruct();
		Result->CreateUserDefinedPin(TEXT("Captured"), Type, EGPD_Input);
		UK2Node_SeinAbilityInputs* Make = NewObject<UK2Node_SeinAbilityInputs>(Graph);
		Make->Operation = 0; Make->AbilityClass = Ability;
		Graph->AddNode(Make); Make->CreateNewGuid(); Make->AllocateDefaultPins();
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		Schema->TryCreateConnection(Entry->FindPinChecked(Schema->PN_Then), Result->FindPinChecked(Schema->PN_Execute));
		Schema->TryCreateConnection(Make->FindPinChecked(TEXT("ReturnValue")), Result->FindPinChecked(TEXT("Captured")));
		FEdGraphPinType IntType; IntType.PinCategory = Schema->PC_Int;
		Result->CreateUserDefinedPin(TEXT("ReadValue"), IntType, EGPD_Input);
		UK2Node_CallFunction* Read = NewObject<UK2Node_CallFunction>(Graph);
		Read->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(USeinAbilityInputBPFL, GetInput), USeinAbilityInputBPFL::StaticClass());
		Graph->AddNode(Read); Read->CreateNewGuid(); Read->AllocateDefaultPins();
		Read->FindPinChecked(TEXT("AbilityClass"))->DefaultObject = Ability;
		Read->FindPinChecked(TEXT("Name"))->DefaultValue = TEXT("QueueIndex");
		Read->FindPinChecked(TEXT("Value"))->PinType = IntType;
		Schema->TryCreateConnection(Make->FindPinChecked(TEXT("ReturnValue")), Read->FindPinChecked(TEXT("Inputs")));
		Schema->TryCreateConnection(Read->FindPinChecked(TEXT("Value")), Result->FindPinChecked(TEXT("ReadValue")));
		return Make;
	}
}

namespace UE::SeinARTSTests
{
	TEST(EachActionHasItsOwnMenuIdentity, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
		Database.RefreshClassActions(UK2Node_SeinAbilityInputs::StaticClass());
		const auto* Actions = Database.GetAllActions().Find(FObjectKey(UK2Node_SeinAbilityInputs::StaticClass()));
		ASSERT_THAT(IsNotNull(Actions));
		ASSERT_THAT(AreEqual(3, Actions->Num()));
		TSet<FGuid> Signatures;
		TSet<FString> Titles;
		for (UBlueprintNodeSpawner* Spawner : *Actions)
		{
			Signatures.Add(Spawner->GetSpawnerSignature().AsGuid());
			Titles.Add(Spawner->GetTemplateNode()->GetNodeTitle(ENodeTitleType::MenuTitle).ToString());
		}
		ASSERT_THAT(AreEqual(3, Signatures.Num()));
		ASSERT_THAT(IsTrue(Titles.Contains(TEXT("Make Ability Inputs"))));
		ASSERT_THAT(IsTrue(Titles.Contains(TEXT("Activate Ability"))));
		ASSERT_THAT(IsTrue(Titles.Contains(TEXT("Issue Ability"))));
	}

	TEST(CompiledPinsPreserveLiteralAndContainerDefaults, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		UBlueprint* Ability = MakeBlueprint(USeinAbility::StaticClass());
		FEdGraphPinType IntType; IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
		AddInput(Ability, TEXT("QueueIndex"), IntType, TEXT("7"));
		FEdGraphPinType ArrayType = IntType; ArrayType.ContainerType = EPinContainerType::Array;
		AddInput(Ability, TEXT("Choices"), ArrayType);
		FEdGraphPinType VectorType; VectorType.PinCategory = UEdGraphSchema_K2::PC_Struct; VectorType.PinSubCategoryObject = FFixedVector::StaticStruct();
		AddInput(Ability, TEXT("Offset"), VectorType);
		FKismetEditorUtilities::CompileBlueprint(Ability);
		ASSERT_THAT(IsTrue(Ability->Status == BS_UpToDate));
		USeinAbility* Defaults = Ability->GeneratedClass->GetDefaultObject<USeinAbility>();
		FArrayProperty* Array = FindFProperty<FArrayProperty>(Ability->GeneratedClass, TEXT("Choices"));
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Defaults));
		Helper.AddValues(2); *reinterpret_cast<int32*>(Helper.GetRawPtr(0)) = 3; *reinterpret_cast<int32*>(Helper.GetRawPtr(1)) = 5;
		FStructProperty* Vector = FindFProperty<FStructProperty>(Ability->GeneratedClass, TEXT("Offset"));
		Vector->CopyCompleteValue(Vector->ContainerPtrToValuePtr<void>(Defaults), &FFixedVector::ZeroVector);
		UBlueprint* Caller = MakeBlueprint(UObject::StaticClass());
		UK2Node_SeinAbilityInputs* Node = Builder(Caller, Ability->GeneratedClass);
		Node->FindPinChecked(TEXT("Input_QueueIndex"))->DefaultValue = TEXT("19");
		FKismetEditorUtilities::CompileBlueprint(Caller);
		ASSERT_THAT(IsTrue(Caller->Status == BS_UpToDate));
		TStrongObjectPtr<UObject> Object(NewObject<UObject>(GetTransientPackage(), Caller->GeneratedClass));
		UFunction* Function = Object->FindFunction(TEXT("BuildInputs"));
		ASSERT_THAT(IsNotNull(Function));
		FStructOnScope Params(Function);
		Object->ProcessEvent(Function, Params.GetStructMemory());
		ASSERT_THAT(AreEqual(19, FindFProperty<FIntProperty>(Function, TEXT("ReadValue"))->GetPropertyValue_InContainer(Params.GetStructMemory())));
		const FSeinAbilityActivationInputs* Inputs = FindFProperty<FStructProperty>(Function, TEXT("Captured"))->ContainerPtrToValuePtr<FSeinAbilityActivationInputs>(Params.GetStructMemory());
		TStrongObjectPtr<USeinAbility> Decoded(NewObject<USeinAbility>(GetTransientPackage(), Ability->GeneratedClass));
		FString Error;
		ASSERT_THAT(IsTrue(Inputs->Decode(*Decoded, Error)));
		ASSERT_THAT(AreEqual(19, FindFProperty<FIntProperty>(Ability->GeneratedClass, TEXT("QueueIndex"))->GetPropertyValue_InContainer(Decoded.Get())));
		FScriptArrayHelper Actual(Array, Array->ContainerPtrToValuePtr<void>(Decoded.Get()));
		ASSERT_THAT(AreEqual(2, Actual.Num()));
		ASSERT_THAT(AreEqual(5, *reinterpret_cast<int32*>(Actual.GetRawPtr(1))));
	}

	TEST(ConnectedNativeBitfieldConvertsToActivationBoolean, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		UBlueprint* Ability = MakeBlueprint(USeinAbility::StaticClass());
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		AddInput(Ability, TEXT("QueueIndex"), Type);
		FKismetEditorUtilities::CompileBlueprint(Ability);
		UBlueprint* Caller = MakeBlueprint(UActorComponent::StaticClass());
		UK2Node_SeinAbilityInputs* Make = Builder(Caller, Ability->GeneratedClass);
		UEdGraph* Graph = Make->GetGraph();
		UK2Node_VariableGet* Get = NewObject<UK2Node_VariableGet>(Graph);
		Get->VariableReference.SetSelfMember(TEXT("bAutoActivate"));
		Graph->AddNode(Get); Get->CreateNewGuid(); Get->AllocateDefaultPins();
		ASSERT_THAT(IsTrue(GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Get->GetValuePin(), Make->FindPinChecked(TEXT("Input_QueueIndex")))));
		FKismetEditorUtilities::CompileBlueprint(Caller);
		ASSERT_THAT(IsTrue(Caller->Status == BS_UpToDate));
		TStrongObjectPtr<UObject> Object(NewObject<UObject>(GetTransientPackage(), Caller->GeneratedClass));
		FBoolProperty* Source = FindFProperty<FBoolProperty>(Caller->GeneratedClass, TEXT("bAutoActivate"));
		ASSERT_THAT(IsFalse(Source->IsNativeBool()));
		UFunction* Function = Object->FindFunction(TEXT("BuildInputs"));
		for (bool Expected : {false, true})
		{
			Source->SetPropertyValue_InContainer(Object.Get(), Expected);
			FStructOnScope Params(Function);
			Object->ProcessEvent(Function, Params.GetStructMemory());
			const auto* Inputs = FindFProperty<FStructProperty>(Function, TEXT("Captured"))->ContainerPtrToValuePtr<FSeinAbilityActivationInputs>(Params.GetStructMemory());
			TStrongObjectPtr<USeinAbility> Decoded(NewObject<USeinAbility>(GetTransientPackage(), Ability->GeneratedClass));
			FString Error;
			ASSERT_THAT(IsTrue(Inputs->Decode(*Decoded, Error)));
			ASSERT_THAT(AreEqual(Expected, FindFProperty<FBoolProperty>(Ability->GeneratedClass, TEXT("QueueIndex"))->GetPropertyValue_InContainer(Decoded.Get())));
		}
	}

	TEST(StaleCompiledSchemaAbortsBeforeReadingChangedValueType, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		UBlueprint* Ability = MakeBlueprint(USeinAbility::StaticClass());
		FEdGraphPinType IntType; IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
		AddInput(Ability, TEXT("QueueIndex"), IntType, TEXT("7"));
		FKismetEditorUtilities::CompileBlueprint(Ability);
		UBlueprint* Caller = MakeBlueprint(UObject::StaticClass());
		UK2Node_SeinAbilityInputs* Make = Builder(Caller, Ability->GeneratedClass);
		UEdGraph* Graph = Make->GetGraph();
		UK2Node_CallFunction* Set = NewObject<UK2Node_CallFunction>(Graph);
		Set->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(USeinAbilityInputBPFL, SetInput), USeinAbilityInputBPFL::StaticClass());
		Graph->AddNode(Set); Set->CreateNewGuid(); Set->AllocateDefaultPins();
		Set->FindPinChecked(TEXT("AbilityClass"))->DefaultObject = Ability->GeneratedClass;
		Set->FindPinChecked(TEXT("Name"))->DefaultValue = TEXT("QueueIndex");
		// Simulate previously compiled string storage targeting a current int.
		Set->FindPinChecked(TEXT("Value"))->PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		Set->FindPinChecked(TEXT("Value"))->DefaultValue = TEXT("This must never be evaluated into int storage");
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		Schema->TryCreateConnection(Make->FindPinChecked(TEXT("ReturnValue")), Set->FindPinChecked(TEXT("Inputs")));
		for (UEdGraphNode* N : Graph->Nodes)
			if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(N))
			{
				Result->FindPinChecked(TEXT("Captured"))->BreakAllPinLinks();
				Schema->TryCreateConnection(Set->GetReturnValuePin(), Result->FindPinChecked(TEXT("Captured")));
			}
		FKismetEditorUtilities::CompileBlueprint(Caller);
		ASSERT_THAT(IsTrue(Caller->Status == BS_UpToDate));
		TStrongObjectPtr<UObject> Object(NewObject<UObject>(GetTransientPackage(), Caller->GeneratedClass));
		UFunction* Function = Object->FindFunction(TEXT("BuildInputs"));
		FStructOnScope Params(Function);
		bool Aborted = false;
		FDelegateHandle Handle = FBlueprintCoreDelegates::OnScriptException.AddLambda(
			[&Aborted](const UObject*, const FFrame&, const FBlueprintExceptionInfo& Info)
			{ Aborted |= Info.GetType() == EBlueprintExceptionType::AbortExecution; });
		Object->ProcessEvent(Function, Params.GetStructMemory());
		FBlueprintCoreDelegates::OnScriptException.Remove(Handle);
		ASSERT_THAT(IsTrue(Aborted));
	}

	TEST(UnsupportedExposureFailsAbilityCompilation, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		UBlueprint* Ability = MakeBlueprint(USeinAbility::StaticClass());
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = FInstancedStruct::StaticStruct();
		AddInput(Ability, TEXT("Opaque"), Type);
		TestRunner->AddExpectedError(TEXT("cannot use fixed C arrays"), EAutomationExpectedErrorFlags::Contains, 0);
		FKismetEditorUtilities::CompileBlueprint(Ability);
		ASSERT_THAT(IsTrue(Ability->Status == BS_Error));
	}

	TEST(ExposureRefreshPreservesRenamedVariableIdentity, "SeinARTS.Editor.Blueprint.ActivationInputs")
	{
		UBlueprint* Ability = MakeBlueprint(USeinAbility::StaticClass());
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		AddInput(Ability, TEXT("QueueIndex"), Type, TEXT("7"));
		FKismetEditorUtilities::CompileBlueprint(Ability);
		UBlueprint* Caller = MakeBlueprint(UObject::StaticClass());
		UK2Node_SeinAbilityInputs* Node = Builder(Caller, Ability->GeneratedClass);
		UEdGraphPin* Before = Node->FindPinChecked(TEXT("Input_QueueIndex"));
		FindFProperty<FIntProperty>(Ability->GeneratedClass, TEXT("QueueIndex"))->SetPropertyValue_InContainer(Ability->GeneratedClass->GetDefaultObject(), 9);
		ASSERT_THAT(IsTrue(Node->RefreshInputPins()));
		ASSERT_THAT(AreEqual(FString(TEXT("9")), Before->DefaultValue));
		Before->DefaultValue = TEXT("23");
		FindFProperty<FIntProperty>(Ability->GeneratedClass, TEXT("QueueIndex"))->SetPropertyValue_InContainer(Ability->GeneratedClass->GetDefaultObject(), 11);
		ASSERT_THAT(IsTrue(Node->RefreshInputPins()));
		ASSERT_THAT(AreEqual(FString(TEXT("23")), Before->DefaultValue));
		const FGuid Identity = Before->PersistentGuid;
		ASSERT_THAT(IsTrue(Identity.IsValid()));
		FBlueprintEditorUtils::RenameMemberVariable(Ability, TEXT("QueueIndex"), TEXT("EntryIndex"));
		FKismetEditorUtilities::CompileBlueprint(Ability);
		FTSTicker::GetCoreTicker().Tick(0.01f);
		UEdGraphPin* After = Node->FindPin(TEXT("Input_EntryIndex"));
		ASSERT_THAT(IsNotNull(After));
		ASSERT_THAT(IsTrue(After->PersistentGuid == Identity));
		ASSERT_THAT(AreEqual(FString(TEXT("23")), After->DefaultValue));
		AddInput(Ability, TEXT("Count"), Type, TEXT("2"));
		FKismetEditorUtilities::CompileBlueprint(Ability);
		FTSTicker::GetCoreTicker().Tick(0.01f);
		ASSERT_THAT(IsNotNull(Node->FindPin(TEXT("Input_Count"))));
	}
}
