#include "CQTest.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Abilities/SeinMoveToProxy.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/DataValidation.h"
#include "TestTypes/SeinAbilityContinuationValidationTestTypes.h"
#include "UObject/Package.h"
#include "Validators/SeinAbilityContinuationValidator.h"

namespace UE::SeinARTSTests::MoveToContinuationValidation
{
	const FName DiagnosticToken(TEXT("SEIN-MOVETO-CONTINUATION"));
	const FName CheckpointDiagnosticToken(
		TEXT("SEIN-CHECKPOINT-CONTINUATION"));
	const FName EventName(TEXT("RunContinuationProbe"));
	const FName IndependentEventName(
		TEXT("RunIndependentContinuationProbe"));
	const FName ObservedValueName(TEXT("ObservedValue"));
	const FName PersistedValueName(TEXT("PersistedValue"));
	const FName CrossingValueName(TEXT("CrossingValue"));
	const FName ObservedResultName(TEXT("ObservedResult"));
	const FName PersistedResultName(TEXT("PersistedResult"));
	const FName SafeHelperName(TEXT("ReadPersistedAfterResume"));
	const FName TransientHelperName(TEXT("PromoteIntoTransientHelper"));
	const FName HelperResultName(TEXT("InputResult"));
	const FName UnsafeMapName(TEXT("UnsafeMap"));
	const FName ObservedMapName(TEXT("ObservedMap"));

	struct FFixture
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		UK2Node_CustomEvent* Entry = nullptr;
		UK2Node_AsyncAction* MoveTo = nullptr;
	};

	void AddNode(UEdGraph& Graph, UEdGraphNode& Node)
	{
		Graph.AddNode(&Node, false, false);
		Node.CreateNewGuid();
		Node.PostPlacedNewNode();
		Node.AllocateDefaultPins();
	}

	FFixture MakeFixture(
		TSubclassOf<UBlueprint> BlueprintClass,
		TSubclassOf<USeinAbility> ParentClass =
			USeinAbility::StaticClass())
	{
		FFixture Result;
		const FName BlueprintName = MakeUniqueObjectName(
			GetTransientPackage(), BlueprintClass.Get(),
			TEXT("BP_SeinMoveToContinuationValidation"));
		Result.Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			GetTransientPackage(),
			BlueprintName,
			BPTYPE_Normal,
			BlueprintClass,
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		if (!Result.Blueprint)
		{
			return Result;
		}

		Result.Graph =
			FBlueprintEditorUtils::FindEventGraph(Result.Blueprint);
		if (!Result.Graph)
		{
			return Result;
		}

		Result.Entry = NewObject<UK2Node_CustomEvent>(Result.Graph);
		Result.Entry->CustomFunctionName = EventName;
		AddNode(*Result.Graph, *Result.Entry);

		const UFunction* Factory =
			USeinMoveToProxy::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(USeinMoveToProxy, SeinMoveTo));
		if (!Factory)
		{
			return Result;
		}
		Result.MoveTo = NewObject<UK2Node_AsyncAction>(Result.Graph);
		Result.MoveTo->InitializeProxyFromFunction(Factory);
		AddNode(*Result.Graph, *Result.MoveTo);

		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (!Schema->TryCreateConnection(
				Result.Entry->FindPinChecked(
					UEdGraphSchema_K2::PN_Then),
				Result.MoveTo->FindPinChecked(
					UEdGraphSchema_K2::PN_Execute)))
		{
			Result.MoveTo = nullptr;
		}
		return Result;
	}

	FEdGraphPinType IntPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		return Type;
	}

	FEdGraphPinType MoveResultPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = FSeinMoveToResult::StaticStruct();
		return Type;
	}

	FEdGraphPinType UnsafeMapPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		Type.ContainerType = EPinContainerType::Map;
		Type.PinValueType.TerminalCategory =
			UEdGraphSchema_K2::PC_Real;
		Type.PinValueType.TerminalSubCategory =
			UEdGraphSchema_K2::PC_Float;
		return Type;
	}

	void AddObservedIntMember(UBlueprint& Blueprint)
	{
		FBlueprintEditorUtils::AddMemberVariable(
			&Blueprint, ObservedValueName, IntPinType());
	}

	UK2Node_VariableSet* AddIntSetter(
		FFixture& Fixture,
		FName VariableName,
		FName CallbackName)
	{
		UK2Node_VariableSet* Setter =
			NewObject<UK2Node_VariableSet>(Fixture.Graph);
		Setter->VariableReference.SetSelfMember(VariableName);
		AddNode(*Fixture.Graph, *Setter);

		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (!Schema->TryCreateConnection(
				Fixture.MoveTo->FindPinChecked(CallbackName),
				Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute)))
		{
			return nullptr;
		}
		return Setter;
	}

	UK2Node_CallFunction* AddCallAfterCallback(
		FFixture& Fixture,
		const UFunction& Function,
		FName CallbackName)
	{
		UK2Node_CallFunction* Call =
			NewObject<UK2Node_CallFunction>(Fixture.Graph);
		Call->SetFromFunction(&Function);
		AddNode(*Fixture.Graph, *Call);
		if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
				Fixture.MoveTo->FindPinChecked(CallbackName),
				Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute)))
		{
			return nullptr;
		}
		return Call;
	}

	UK2Node_AsyncAction* AddSupportedMoveToAfter(
		FFixture& Fixture,
		UEdGraphPin& SourceExec)
	{
		const UFunction* Factory =
			USeinMoveToProxy::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinMoveToProxy,
					SeinMoveTo));
		if (!Factory)
		{
			return nullptr;
		}

		UK2Node_AsyncAction* Async =
			NewObject<UK2Node_AsyncAction>(Fixture.Graph);
		Async->InitializeProxyFromFunction(Factory);
		AddNode(*Fixture.Graph, *Async);
		if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
				&SourceExec,
				Async->FindPinChecked(
					UEdGraphSchema_K2::PN_Execute)))
		{
			return nullptr;
		}
		return Async;
	}

	UK2Node_AsyncAction* AddHeterogeneousAsyncAfter(
		FFixture& Fixture,
		UEdGraphPin& SourceExec)
	{
		const UFunction* Factory =
			USeinAbilityContinuationValidationHeterogeneousAsyncProxy::
				StaticClass()->FindFunctionByName(
					GET_FUNCTION_NAME_CHECKED(
						USeinAbilityContinuationValidationHeterogeneousAsyncProxy,
						StartHeterogeneousValidationAsync));
		if (!Factory)
		{
			return nullptr;
		}

		UK2Node_AsyncAction* Async =
			NewObject<UK2Node_AsyncAction>(Fixture.Graph);
		Async->InitializeProxyFromFunction(Factory);
		AddNode(*Fixture.Graph, *Async);
		if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
				&SourceExec,
				Async->FindPinChecked(
					UEdGraphSchema_K2::PN_Execute)))
		{
			return nullptr;
		}
		return Async;
	}

	const UFunction* AddSafeSameBlueprintHelper(FFixture& Fixture)
	{
		UEdGraph* HelperGraph = FBlueprintEditorUtils::CreateNewGraph(
			Fixture.Blueprint,
			SafeHelperName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!HelperGraph)
		{
			return nullptr;
		}
		FBlueprintEditorUtils::AddFunctionGraph(
			Fixture.Blueprint,
			HelperGraph,
			true,
			static_cast<UFunction*>(nullptr));

		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : HelperGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* Candidate =
					Cast<UK2Node_FunctionEntry>(Node))
			{
				Entry = Candidate;
				break;
			}
		}
		if (!Entry)
		{
			return nullptr;
		}

		UK2Node_VariableGet* Getter =
			NewObject<UK2Node_VariableGet>(HelperGraph);
		Getter->VariableReference.SetSelfMember(PersistedValueName);
		AddNode(*HelperGraph, *Getter);
		UK2Node_VariableSet* Setter =
			NewObject<UK2Node_VariableSet>(HelperGraph);
		Setter->VariableReference.SetSelfMember(ObservedValueName);
		AddNode(*HelperGraph, *Setter);

		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (!Schema->TryCreateConnection(
				Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
				Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))
			|| !Schema->TryCreateConnection(
				Getter->GetValuePin(),
				Setter->FindPinChecked(ObservedValueName)))
		{
			return nullptr;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
			Fixture.Blueprint);
		return Fixture.Blueprint->SkeletonGeneratedClass
			? Fixture.Blueprint->SkeletonGeneratedClass
				->FindFunctionByName(SafeHelperName)
			: nullptr;
	}

	const UFunction* AddTransientPromotionHelper(FFixture& Fixture)
	{
		UEdGraph* HelperGraph = FBlueprintEditorUtils::CreateNewGraph(
			Fixture.Blueprint,
			TransientHelperName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!HelperGraph)
		{
			return nullptr;
		}
		FBlueprintEditorUtils::AddFunctionGraph(
			Fixture.Blueprint,
			HelperGraph,
			true,
			static_cast<UFunction*>(nullptr));

		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : HelperGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* Candidate =
					Cast<UK2Node_FunctionEntry>(Node))
			{
				Entry = Candidate;
				break;
			}
		}
		if (!Entry)
		{
			return nullptr;
		}
		UEdGraphPin* ResultInput = Entry->CreateUserDefinedPin(
			HelperResultName,
			MoveResultPinType(),
			EGPD_Output);

		UK2Node_VariableSet* Setter =
			NewObject<UK2Node_VariableSet>(HelperGraph);
		Setter->VariableReference.SetSelfMember(
			GET_MEMBER_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				UnsafeTransientResult));
		AddNode(*HelperGraph, *Setter);

		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (!ResultInput
			|| !Schema->TryCreateConnection(
				Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),
				Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))
			|| !Schema->TryCreateConnection(
				ResultInput,
				Setter->FindPinChecked(GET_MEMBER_NAME_CHECKED(
					USeinAbilityContinuationValidationTestAbility,
					UnsafeTransientResult))))
		{
			return nullptr;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
			Fixture.Blueprint);
		return Fixture.Blueprint->SkeletonGeneratedClass
			? Fixture.Blueprint->SkeletonGeneratedClass
				->FindFunctionByName(TransientHelperName)
			: nullptr;
	}

	UEdGraphPin* SplitMoveResultField(
		UK2Node_AsyncAction& Async,
		const TCHAR* FieldName)
	{
		UEdGraphPin* ResultPin = Async.FindPin(TEXT("Result"));
		if (!ResultPin)
		{
			return nullptr;
		}
		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (ResultPin->SubPins.IsEmpty())
		{
			Schema->SplitPin(ResultPin, false);
		}
		for (UEdGraphPin* Child : ResultPin->SubPins)
		{
			if (Child
				&& Child->PinName.ToString().EndsWith(
					FString(FieldName)))
			{
				return Child;
			}
		}
		return nullptr;
	}

	FFixture MakeTransientPromotionFixture(
		TSubclassOf<UBlueprint> BlueprintClass)
	{
		FFixture Fixture = MakeFixture(
			BlueprintClass,
			USeinAbilityContinuationValidationTestAbility::StaticClass());
		if (!Fixture.Blueprint || !Fixture.MoveTo)
		{
			return Fixture;
		}

		UK2Node_VariableSet* Setter =
			NewObject<UK2Node_VariableSet>(Fixture.Graph);
		Setter->VariableReference.SetSelfMember(
			GET_MEMBER_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				UnsafeTransientResult));
		AddNode(*Fixture.Graph, *Setter);
		const UEdGraphSchema_K2* Schema =
			GetDefault<UEdGraphSchema_K2>();
		if (!Schema->TryCreateConnection(
				Fixture.MoveTo->FindPinChecked(
					GET_MEMBER_NAME_CHECKED(
						USeinMoveToProxy,
						OnFailed)),
				Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))
			|| !Schema->TryCreateConnection(
				Fixture.MoveTo->FindPinChecked(TEXT("Result")),
				Setter->FindPinChecked(GET_MEMBER_NAME_CHECKED(
					USeinAbilityContinuationValidationTestAbility,
					UnsafeTransientResult))))
		{
			Fixture.MoveTo = nullptr;
		}
		return Fixture;
	}

	FCompilerResultsLog Compile(UBlueprint& Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
			&Blueprint);
		FCompilerResultsLog Log;
		Log.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(
			&Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection,
			&Log);
		return Log;
	}

	bool ContainsDiagnostic(const FCompilerResultsLog& Log)
	{
		return Log.Messages.ContainsByPredicate(
			[](const TSharedRef<FTokenizedMessage>& Message)
			{
				const FString Text = Message->ToText().ToString();
				return Text.Contains(DiagnosticToken.ToString())
					|| Text.Contains(
						CheckpointDiagnosticToken.ToString());
			});
	}

	bool ContainsCheckpointDiagnostic(const FCompilerResultsLog& Log)
	{
		return Log.Messages.ContainsByPredicate(
			[](const TSharedRef<FTokenizedMessage>& Message)
			{
				return Message->ToText().ToString().Contains(
					CheckpointDiagnosticToken.ToString());
			});
	}

	UK2Node_AsyncAction* AddAsyncFactoryAfter(
		FFixture& Fixture,
		const UFunction& Factory,
		UEdGraphPin& SourceExec)
	{
		UK2Node_AsyncAction* Async =
			NewObject<UK2Node_AsyncAction>(Fixture.Graph);
		Async->InitializeProxyFromFunction(&Factory);
		AddNode(*Fixture.Graph, *Async);
		if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
				&SourceExec,
				Async->FindPinChecked(
					UEdGraphSchema_K2::PN_Execute)))
		{
			return nullptr;
		}
		return Async;
	}

	FFixture MakeUnsafeEventParameterFixture(
		TSubclassOf<UBlueprint> BlueprintClass)
	{
		FFixture Fixture = MakeFixture(BlueprintClass);
		if (!Fixture.Blueprint || !Fixture.MoveTo)
		{
			return Fixture;
		}
		AddObservedIntMember(*Fixture.Blueprint);

		UEdGraphPin* Parameter = Fixture.Entry->CreateUserDefinedPin(
			CrossingValueName, IntPinType(), EGPD_Output);
		UK2Node_VariableSet* Setter = AddIntSetter(
			Fixture, ObservedValueName,
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted));
		if (!Parameter || !Setter
			|| !GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
				Parameter,
				Setter->FindPinChecked(ObservedValueName)))
		{
			Fixture.MoveTo = nullptr;
		}
		return Fixture;
	}
}

namespace UE::SeinARTSTests
{
TEST(
	MoveToContinuationSafeMemberCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	AddObservedIntMember(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, PersistedValueName, IntPinType())));

	UK2Node_VariableGet* Getter =
		NewObject<UK2Node_VariableGet>(Fixture.Graph);
	Getter->VariableReference.SetSelfMember(PersistedValueName);
	AddNode(*Fixture.Graph, *Getter);
	UK2Node_VariableSet* Setter = AddIntSetter(
		Fixture, ObservedValueName,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted));
	ASSERT_THAT(IsNotNull(Setter));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Getter->GetValuePin(),
			Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors == 0));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationUnrelatedEventParameterCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeUnsafeEventParameterFixture(
		USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationResultPinCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, ObservedResultName, MoveResultPinType())));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Setter);

	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed)),
		Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(TEXT("Result")),
		Setter->FindPinChecked(ObservedResultName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors == 0));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationNestedAsyncOriginalResultFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint,
		ObservedResultName,
		MoveResultPinType())));

	UK2Node_AsyncAction* Nested = AddSupportedMoveToAfter(
		Fixture,
		*Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(
				USeinMoveToProxy,
				OnFailed)));
	ASSERT_THAT(IsNotNull(Nested));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Setter);

	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Nested->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnCompleted)),
		Setter->FindPinChecked(
			UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(TEXT("Result")),
		Setter->FindPinChecked(ObservedResultName))));

	const FCompilerResultsLog CompileLog =
		Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(CompileLog.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(CompileLog)));

	USeinAbilityContinuationValidator* Validator =
		GetMutableDefault<USeinAbilityContinuationValidator>();
	ASSERT_THAT(IsNotNull(Validator));
	FDataValidationContext Context;
	const EDataValidationResult Validation =
		Validator->ValidateLoadedAsset(
			FAssetData(Fixture.Blueprint),
			Fixture.Blueprint,
			Context);
	ASSERT_THAT(
		IsTrue(Validation == EDataValidationResult::Invalid));
	ASSERT_THAT(IsTrue(Context.GetIssues().ContainsByPredicate(
		[](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Message.ToString().Contains(
				DiagnosticToken.ToString());
		})));
}

TEST(
	MoveToContinuationPromotedResultSurvivesNestedAsync,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const FEdGraphPinType ResultType = MoveResultPinType();
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, PersistedResultName, ResultType)));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, ObservedResultName, ResultType)));

	UK2Node_VariableSet* Persist =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Persist->VariableReference.SetSelfMember(PersistedResultName);
	AddNode(*Fixture.Graph, *Persist);
	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(
				USeinMoveToProxy,
				OnFailed)),
		Persist->FindPinChecked(
			UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(TEXT("Result")),
		Persist->FindPinChecked(PersistedResultName))));

	UK2Node_AsyncAction* Nested = AddSupportedMoveToAfter(
		Fixture,
		*Persist->FindPinChecked(UEdGraphSchema_K2::PN_Then));
	ASSERT_THAT(IsNotNull(Nested));

	UK2Node_VariableGet* Getter =
		NewObject<UK2Node_VariableGet>(Fixture.Graph);
	Getter->VariableReference.SetSelfMember(PersistedResultName);
	AddNode(*Fixture.Graph, *Getter);
	UK2Node_VariableSet* Observe =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Observe->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Observe);
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Nested->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnCompleted)),
		Observe->FindPinChecked(
			UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Getter->GetValuePin(),
		Observe->FindPinChecked(ObservedResultName))));

	const FCompilerResultsLog CompileLog =
		Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, CompileLog.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(CompileLog)));

	USeinAbilityContinuationValidator* Validator =
		GetMutableDefault<USeinAbilityContinuationValidator>();
	ASSERT_THAT(IsNotNull(Validator));
	FDataValidationContext Context;
	const EDataValidationResult Validation =
		Validator->ValidateLoadedAsset(
			FAssetData(Fixture.Blueprint),
			Fixture.Blueprint,
			Context);
	ASSERT_THAT(
		IsTrue(Validation == EDataValidationResult::Valid));
	ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
}

TEST(
	MoveToContinuationIndependentSharedResultChainFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint,
		ObservedResultName,
		MoveResultPinType())));

	UK2Node_CustomEvent* Independent =
		NewObject<UK2Node_CustomEvent>(Fixture.Graph);
	Independent->CustomFunctionName = IndependentEventName;
	AddNode(*Fixture.Graph, *Independent);
	UK2Node_AsyncAction* First = AddSupportedMoveToAfter(
		Fixture,
		*Independent->FindPinChecked(
			UEdGraphSchema_K2::PN_Then));
	ASSERT_THAT(IsNotNull(First));
	UK2Node_AsyncAction* Second = AddSupportedMoveToAfter(
		Fixture,
		*First->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnCompleted)));
	ASSERT_THAT(IsNotNull(Second));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Setter);
	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Second->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnCompleted)),
		Setter->FindPinChecked(
			UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		First->FindPinChecked(TEXT("Result")),
		Setter->FindPinChecked(ObservedResultName))));

	const FCompilerResultsLog CompileLog =
		Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(CompileLog.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(CompileLog)));

	USeinAbilityContinuationValidator* Validator =
		GetMutableDefault<USeinAbilityContinuationValidator>();
	ASSERT_THAT(IsNotNull(Validator));
	FDataValidationContext Context;
	const EDataValidationResult Validation =
		Validator->ValidateLoadedAsset(
			FAssetData(Fixture.Blueprint),
			Fixture.Blueprint,
			Context);
	ASSERT_THAT(
		IsTrue(Validation == EDataValidationResult::Invalid));
	ASSERT_THAT(IsTrue(Context.GetIssues().ContainsByPredicate(
		[](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Message.ToString().Contains(
				DiagnosticToken.ToString());
		})));
}

TEST(
	MoveToContinuationUnrelatedPureGetterCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);

	const UFunction* GetterFunction =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				ReadUnsafeTransientValue));
	ASSERT_THAT(IsNotNull(GetterFunction));
	UK2Node_CallFunction* Getter =
		NewObject<UK2Node_CallFunction>(Fixture.Graph);
	Getter->SetFromFunction(GetterFunction);
	AddNode(*Fixture.Graph, *Getter);

	UK2Node_VariableSet* Setter = AddIntSetter(
		Fixture, ObservedValueName,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted));
	ASSERT_THAT(IsNotNull(Setter));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Getter->GetReturnValuePin(),
			Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationMetadataOnlyIgnoreMemberCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);

	UK2Node_VariableGet* Getter =
		NewObject<UK2Node_VariableGet>(Fixture.Graph);
	Getter->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(
		USeinAbilityContinuationValidationTestAbility,
		MetadataOnlyIgnoreAttempt));
	AddNode(*Fixture.Graph, *Getter);
	UK2Node_VariableSet* Setter = AddIntSetter(
		Fixture, ObservedValueName,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted));
	ASSERT_THAT(IsNotNull(Setter));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Getter->GetValuePin(),
			Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
}

TEST(
	MoveToContinuationUnrelatedMapValueCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const FEdGraphPinType MapType = UnsafeMapPinType();
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, UnsafeMapName, MapType)));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, ObservedMapName, MapType)));

	UK2Node_VariableGet* Getter =
		NewObject<UK2Node_VariableGet>(Fixture.Graph);
	Getter->VariableReference.SetSelfMember(UnsafeMapName);
	AddNode(*Fixture.Graph, *Getter);
	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedMapName);
	AddNode(*Fixture.Graph, *Setter);

	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted)),
		Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Getter->GetValuePin(),
		Setter->FindPinChecked(ObservedMapName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationUnconnectedImpureNativeCallCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const UFunction* Function =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				RecordUnsafeTransientValue));
	ASSERT_THAT(IsNotNull(Function));
	ASSERT_THAT(IsNotNull(AddCallAfterCallback(
		Fixture,
		*Function,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationSafeSameBlueprintHelperCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint, PersistedValueName, IntPinType())));
	const UFunction* Helper = AddSafeSameBlueprintHelper(Fixture);
	ASSERT_THAT(IsNotNull(Helper));
	ASSERT_THAT(IsNotNull(AddCallAfterCallback(
		Fixture,
		*Helper,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors == 0));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationTransientPromotionFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeTransientPromotionFixture(
		USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationHelperTransientPromotionFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	const UFunction* Helper = AddTransientPromotionHelper(Fixture);
	ASSERT_THAT(IsNotNull(Helper));
	UK2Node_CallFunction* Call = AddCallAfterCallback(
		Fixture,
		*Helper,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed));
	ASSERT_THAT(IsNotNull(Call));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Fixture.MoveTo->FindPinChecked(TEXT("Result")),
			Call->FindPinChecked(HelperResultName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationSafeMarkedLatentFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	const UFunction* Function =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				ConsumeResultInSafeMarkedLatent));
	ASSERT_THAT(IsNotNull(Function));
	UK2Node_CallFunction* Call = AddCallAfterCallback(
		Fixture,
		*Function,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed));
	ASSERT_THAT(IsNotNull(Call));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Fixture.MoveTo->FindPinChecked(TEXT("Result")),
			Call->FindPinChecked(TEXT("Result")))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationHeterogeneousCallbackFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint,
		ObservedResultName,
		MoveResultPinType())));

	UK2Node_AsyncAction* Async = AddHeterogeneousAsyncAfter(
		Fixture,
		*Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(
				USeinMoveToProxy,
				OnCompleted)));
	ASSERT_THAT(IsNotNull(Async));
	UEdGraphPin* SharedResult = Async->FindPin(TEXT("Result"));
	ASSERT_THAT(IsNotNull(SharedResult));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Setter);
	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Async->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinAbilityContinuationValidationHeterogeneousAsyncProxy,
			OnWithoutResult)),
		Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		SharedResult,
		Setter->FindPinChecked(ObservedResultName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationMultiEntryMergeFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
		Fixture.Blueprint,
		ObservedResultName,
		MoveResultPinType())));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedResultName);
	AddNode(*Fixture.Graph, *Setter);
	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnFailed)),
		Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(TEXT("Result")),
		Setter->FindPinChecked(ObservedResultName))));

	UK2Node_CustomEvent* Independent =
		NewObject<UK2Node_CustomEvent>(Fixture.Graph);
	Independent->CustomFunctionName = IndependentEventName;
	AddNode(*Fixture.Graph, *Independent);
	Independent->FindPinChecked(UEdGraphSchema_K2::PN_Then)
		->MakeLinkTo(
			Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationSplitResultFieldCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);

	UEdGraphPin* WaypointIndex = SplitMoveResultField(
		*Fixture.MoveTo,
		TEXT("WaypointIndex"));
	ASSERT_THAT(IsNotNull(WaypointIndex));
	UK2Node_VariableSet* Setter = AddIntSetter(
		Fixture,
		ObservedValueName,
		GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnWaypointReached));
	ASSERT_THAT(IsNotNull(Setter));
	ASSERT_THAT(IsTrue(
		GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			WaypointIndex,
			Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationPureResultTransformCompiles,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);

	const UFunction* Function =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				ExtractWaypointIndex));
	ASSERT_THAT(IsNotNull(Function));
	UK2Node_CallFunction* Transform =
		NewObject<UK2Node_CallFunction>(Fixture.Graph);
	Transform->SetFromFunction(Function);
	AddNode(*Fixture.Graph, *Transform);
	UK2Node_VariableSet* Setter = AddIntSetter(
		Fixture,
		ObservedValueName,
		GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnWaypointReached));
	ASSERT_THAT(IsNotNull(Setter));

	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Fixture.MoveTo->FindPinChecked(TEXT("Result")),
		Transform->FindPinChecked(TEXT("Result")))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Transform->GetReturnValuePin(),
		Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(AreEqual(0, Log.NumErrors));
	ASSERT_THAT(IsFalse(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationSplitResultAcrossNestedAsyncFails,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	AddObservedIntMember(*Fixture.Blueprint);

	UEdGraphPin* WaypointIndex = SplitMoveResultField(
		*Fixture.MoveTo,
		TEXT("WaypointIndex"));
	ASSERT_THAT(IsNotNull(WaypointIndex));
	UK2Node_AsyncAction* Nested = AddSupportedMoveToAfter(
		Fixture,
		*Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(
				USeinMoveToProxy,
				OnWaypointReached)));
	ASSERT_THAT(IsNotNull(Nested));

	UK2Node_VariableSet* Setter =
		NewObject<UK2Node_VariableSet>(Fixture.Graph);
	Setter->VariableReference.SetSelfMember(ObservedValueName);
	AddNode(*Fixture.Graph, *Setter);
	const UEdGraphSchema_K2* Schema =
		GetDefault<UEdGraphSchema_K2>();
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		Nested->FindPinChecked(GET_MEMBER_NAME_CHECKED(
			USeinMoveToProxy,
			OnCompleted)),
		Setter->FindPinChecked(UEdGraphSchema_K2::PN_Execute))));
	ASSERT_THAT(IsTrue(Schema->TryCreateConnection(
		WaypointIndex,
		Setter->FindPinChecked(ObservedValueName))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(Log)));
}

TEST(
	MoveToContinuationGenericBlueprintDataValidator,
	"SeinARTS.Editor.Blueprint.MoveToContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeTransientPromotionFixture(
		UBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));

	// Generic/imported ability Blueprints cannot bypass either gate.
	const FCompilerResultsLog CompileLog = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(CompileLog.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsDiagnostic(CompileLog)));

	USeinAbilityContinuationValidator* Validator =
		GetMutableDefault<USeinAbilityContinuationValidator>();
	ASSERT_THAT(IsNotNull(Validator));
	FDataValidationContext Context;
	const EDataValidationResult Result = Validator->ValidateLoadedAsset(
		FAssetData(Fixture.Blueprint),
		Fixture.Blueprint,
		Context);
	ASSERT_THAT(IsTrue(Result == EDataValidationResult::Invalid));
	ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	ASSERT_THAT(IsTrue(Context.GetIssues().ContainsByPredicate(
		[](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Message.ToString().Contains(
				DiagnosticToken.ToString());
		})));
}

TEST(
	CheckpointContinuationRejectsUndeclaredAsyncFactory,
	"SeinARTS.Editor.Blueprint.CheckpointContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const UFunction* Factory =
		USeinAbilityContinuationValidationAsyncProxy::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationAsyncProxy,
				StartValidationAsync));
	ASSERT_THAT(IsNotNull(Factory));
	ASSERT_THAT(IsNotNull(AddAsyncFactoryAfter(
		Fixture,
		*Factory,
		*Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted)))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsCheckpointDiagnostic(Log)));

	FDataValidationContext Context;
	ASSERT_THAT(IsTrue(
		GetMutableDefault<USeinAbilityContinuationValidator>()
			->ValidateLoadedAsset(
				FAssetData(Fixture.Blueprint),
				Fixture.Blueprint,
				Context)
			== EDataValidationResult::Invalid));
	ASSERT_THAT(IsTrue(Context.GetIssues().ContainsByPredicate(
		[](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Message.ToString().Contains(
				CheckpointDiagnosticToken.ToString());
		})));
}

TEST(
	CheckpointContinuationRejectsUnregisteredDeclaredAction,
	"SeinARTS.Editor.Blueprint.CheckpointContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(USeinAbilityBlueprint::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const UFunction* Factory =
		USeinAbilityContinuationValidationUnregisteredProxy::
			StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinAbilityContinuationValidationUnregisteredProxy,
					StartUnregisteredValidationAsync));
	ASSERT_THAT(IsNotNull(Factory));
	ASSERT_THAT(IsNotNull(AddAsyncFactoryAfter(
		Fixture,
		*Factory,
		*Fixture.MoveTo->FindPinChecked(
			GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted)))));

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsCheckpointDiagnostic(Log)));
}

TEST(
	CheckpointContinuationRejectsUELatentAndTimeline,
	"SeinARTS.Editor.Blueprint.CheckpointContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.MoveTo));
	const UFunction* LatentFunction =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				ConsumeResultInSafeMarkedLatent));
	ASSERT_THAT(IsNotNull(LatentFunction));
	ASSERT_THAT(IsNotNull(AddCallAfterCallback(
		Fixture,
		*LatentFunction,
		GET_MEMBER_NAME_CHECKED(USeinMoveToProxy, OnCompleted))));

	UK2Node_Timeline* Timeline =
		NewObject<UK2Node_Timeline>(Fixture.Graph);
	AddNode(*Fixture.Graph, *Timeline);

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsCheckpointDiagnostic(Log)));
}

TEST(
	CheckpointContinuationRejectsLatentInsideMacro,
	"SeinARTS.Editor.Blueprint.CheckpointContinuation")
{
	using namespace MoveToContinuationValidation;

	FFixture Fixture = MakeFixture(
		USeinAbilityBlueprint::StaticClass(),
		USeinAbilityContinuationValidationTestAbility::StaticClass());
	ASSERT_THAT(IsNotNull(Fixture.Blueprint));
	ASSERT_THAT(IsNotNull(Fixture.Graph));
	const UFunction* LatentFunction =
		USeinAbilityContinuationValidationTestAbility::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
				USeinAbilityContinuationValidationTestAbility,
				ConsumeResultInSafeMarkedLatent));
	ASSERT_THAT(IsNotNull(LatentFunction));

	UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(
		Fixture.Blueprint,
		TEXT("CheckpointUnsafeMacro"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	ASSERT_THAT(IsNotNull(MacroGraph));
	FBlueprintEditorUtils::AddMacroGraph(
		Fixture.Blueprint,
		MacroGraph,
		true,
		nullptr);
	UK2Node_CallFunction* LatentCall =
		NewObject<UK2Node_CallFunction>(MacroGraph);
	LatentCall->SetFromFunction(LatentFunction);
	AddNode(*MacroGraph, *LatentCall);

	UK2Node_MacroInstance* Macro =
		NewObject<UK2Node_MacroInstance>(Fixture.Graph);
	Macro->SetMacroGraph(MacroGraph);
	AddNode(*Fixture.Graph, *Macro);

	const FCompilerResultsLog Log = Compile(*Fixture.Blueprint);
	ASSERT_THAT(IsTrue(Log.NumErrors > 0));
	ASSERT_THAT(IsTrue(ContainsCheckpointDiagnostic(Log)));
}
}
