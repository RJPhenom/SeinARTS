#include "CQTest.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinMoveToProxy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/FieldIterator.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UE::SeinARTSTests
{
	namespace MoveToAsyncCompiler
	{
		const FName EntryPointName(TEXT("RunMoveToCompilerProbe"));
		const FName ObservedResultName(TEXT("ObservedMoveToResult"));

		const TArray<FName>& DelegateNames()
		{
			static const TArray<FName> Names{
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted),
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed),
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnWaypointReached),
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCancelled),
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnPartialPath),
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnPathRecomputed),
			};
			return Names;
		}

		void AddNode(UEdGraph& Graph, UEdGraphNode& Node)
		{
			Graph.AddNode(&Node, false, false);
			Node.CreateNewGuid();
			Node.PostPlacedNewNode();
			Node.AllocateDefaultPins();
		}

		TArray<FName> FindGeneratedCallbacks(const UClass* GeneratedClass)
		{
			TArray<FName> Result;
			for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper);
				It; ++It)
			{
				const FString FunctionName = It->GetName();
				for (const FName DelegateName : DelegateNames())
				{
					if (FunctionName.StartsWith(DelegateName.ToString() + TEXT("_")))
					{
						Result.Add(It->GetFName());
						break;
					}
				}
			}
			Result.Sort(FNameLexicalLess());
			return Result;
		}

		bool HasOneCallbackForEveryDelegate(const TArray<FName>& CallbackNames)
		{
			if (CallbackNames.Num() != DelegateNames().Num())
			{
				return false;
			}

			for (const FName DelegateName : DelegateNames())
			{
				const FString Prefix = DelegateName.ToString() + TEXT("_");
				if (!CallbackNames.ContainsByPredicate(
						[&Prefix](const FName Name)
						{
							return Name.ToString().StartsWith(Prefix);
						}))
				{
					return false;
				}
			}
			return true;
		}
	}

	TEST(MoveToAsyncCompilerContract, "SeinARTS.Editor.Blueprint")
	{
		using namespace MoveToAsyncCompiler;

		const FName BlueprintName = MakeUniqueObjectName(
			GetTransientPackage(), UBlueprint::StaticClass(),
			TEXT("BP_SeinMoveToCompilerProbe"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			USeinAbility::StaticClass(),
			GetTransientPackage(),
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		ASSERT_THAT(IsNotNull(Blueprint));

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(EventGraph));

		FEdGraphPinType ResultPinType;
		ResultPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		ResultPinType.PinSubCategoryObject = FSeinMoveToResult::StaticStruct();
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, ObservedResultName, ResultPinType)));

		UK2Node_CustomEvent* EntryPoint = NewObject<UK2Node_CustomEvent>(EventGraph);
		EntryPoint->CustomFunctionName = EntryPointName;
		AddNode(*EventGraph, *EntryPoint);

		const UFunction* FactoryFunction =
			USeinMoveToProxy::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(USeinMoveToProxy, SeinMoveTo));
		ASSERT_THAT(IsNotNull(FactoryFunction));

		UK2Node_AsyncAction* MoveToNode = NewObject<UK2Node_AsyncAction>(EventGraph);
		MoveToNode->InitializeProxyFromFunction(FactoryFunction);
		AddNode(*EventGraph, *MoveToNode);

		for (const FName DelegateName : DelegateNames())
		{
			const UEdGraphPin* ExecPin = MoveToNode->FindPin(DelegateName);
			ASSERT_THAT(IsNotNull(ExecPin));
			ASSERT_THAT(IsTrue(ExecPin->Direction == EGPD_Output));
			ASSERT_THAT(IsTrue(
				ExecPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec));
		}

		const UEdGraphPin* ResultPin = MoveToNode->FindPin(TEXT("Result"));
		ASSERT_THAT(IsNotNull(ResultPin));
		ASSERT_THAT(IsTrue(ResultPin->Direction == EGPD_Output));
		ASSERT_THAT(IsTrue(
			ResultPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct));
		ASSERT_THAT(IsTrue(
			ResultPin->PinType.PinSubCategoryObject.Get()
			== FSeinMoveToResult::StaticStruct()));

		UK2Node_VariableSet* CaptureResult = NewObject<UK2Node_VariableSet>(EventGraph);
		CaptureResult->VariableReference.SetSelfMember(ObservedResultName);
		AddNode(*EventGraph, *CaptureResult);

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
			EntryPoint->FindPinChecked(UEdGraphSchema_K2::PN_Then),
			MoveToNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
		ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
			MoveToNode->FindPinChecked(
				GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed)),
			CaptureResult->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
		ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
			MoveToNode->FindPinChecked(TEXT("Result")),
			CaptureResult->FindPinChecked(ObservedResultName))));

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		FCompilerResultsLog FirstCompileLog;
		FirstCompileLog.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint, EBlueprintCompileOptions::SkipGarbageCollection,
			&FirstCompileLog);
		ASSERT_THAT(IsTrue(FirstCompileLog.NumErrors == 0));
		ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));

		const TArray<FName> FirstCallbackNames =
			FindGeneratedCallbacks(Blueprint->GeneratedClass);
		ASSERT_THAT(IsTrue(HasOneCallbackForEveryDelegate(FirstCallbackNames)));
		for (const FName CallbackName : FirstCallbackNames)
		{
			const UFunction* Callback =
				Blueprint->GeneratedClass->FindFunctionByName(CallbackName);
			ASSERT_THAT(IsNotNull(Callback));
			ASSERT_THAT(IsTrue(
				Callback->GetOuterUClass() == Blueprint->GeneratedClass));
		}

		FCompilerResultsLog SecondCompileLog;
		SecondCompileLog.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint, EBlueprintCompileOptions::SkipGarbageCollection,
			&SecondCompileLog);
		ASSERT_THAT(IsTrue(SecondCompileLog.NumErrors == 0));
		ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));

		const TArray<FName> SecondCallbackNames =
			FindGeneratedCallbacks(Blueprint->GeneratedClass);
		ASSERT_THAT(IsTrue(FirstCallbackNames == SecondCallbackNames));

		USeinAbility* AbilityInstance = NewObject<USeinAbility>(
			GetTransientPackage(), Blueprint->GeneratedClass);
		ASSERT_THAT(IsNotNull(AbilityInstance));

		UFunction* EntryPointFunction =
			Blueprint->GeneratedClass->FindFunctionByName(EntryPointName);
		ASSERT_THAT(IsNotNull(EntryPointFunction));
		AbilityInstance->ProcessEvent(EntryPointFunction, nullptr);

		const FStructProperty* ObservedResultProperty =
			FindFProperty<FStructProperty>(
				Blueprint->GeneratedClass, ObservedResultName);
		ASSERT_THAT(IsNotNull(ObservedResultProperty));
		ASSERT_THAT(IsTrue(
			ObservedResultProperty->Struct == FSeinMoveToResult::StaticStruct()));

		const FSeinMoveToResult* ObservedResult =
			ObservedResultProperty->ContainerPtrToValuePtr<FSeinMoveToResult>(
				AbilityInstance);
		ASSERT_THAT(IsNotNull(ObservedResult));
		ASSERT_THAT(IsTrue(
			ObservedResult->FailureReason
			== ESeinMoveFailureReason::EntityDestroyed));
	}
}
