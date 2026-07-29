/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContextFreeRecipeDeterminism.cpp
 */

#include "Validators/SeinContextFreeRecipeDeterminism.h"

#include "BlueprintGameplayTagLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameplayTagContainer.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Simulation/SeinCanonicalStateRecipe.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "SeinContextFreeRecipeDeterminism"

namespace
{
	const FName SeinDeterministicMeta(TEXT("SeinDeterministic"));
	const FName LatentMeta(TEXT("Latent"));
	const FName WorldContextMeta(TEXT("WorldContext"));
	const FName DefaultToSelfMeta(TEXT("DefaultToSelf"));

	const FName DeclareRecipeFunction(
		GET_FUNCTION_NAME_CHECKED(
			USeinCanonicalStateRecipe,
			DeclareCanonicalStateSlots));
	const FName MaterializeRecipeFunction(
		GET_FUNCTION_NAME_CHECKED(
			USeinCanonicalStateRecipe,
			MaterializeCanonicalStateValues));

	bool IsSupportedDeterministicStruct(const UObject* TypeObject)
	{
		const UScriptStruct* Struct =
			Cast<UScriptStruct>(TypeObject);
		return Struct
			&& (Struct->HasMetaData(SeinDeterministicMeta)
				|| Struct == FGameplayTag::StaticStruct()
				|| Struct == FGameplayTagContainer::StaticStruct()
				|| Struct == FInstancedStruct::StaticStruct());
	}

	bool IsDeterministicTerminalType(
		FName Category,
		const UObject* TypeObject)
	{
		return Category == UEdGraphSchema_K2::PC_Boolean
			|| Category == UEdGraphSchema_K2::PC_Byte
			|| Category == UEdGraphSchema_K2::PC_Int
			|| Category == UEdGraphSchema_K2::PC_Int64
			|| Category == UEdGraphSchema_K2::PC_Name
			|| Category == UEdGraphSchema_K2::PC_Enum
			|| (Category == UEdGraphSchema_K2::PC_Struct
				&& IsSupportedDeterministicStruct(TypeObject));
	}

	bool IsDeterministicPinType(const FEdGraphPinType& Type)
	{
		if (!IsDeterministicTerminalType(
			Type.PinCategory,
			Type.PinSubCategoryObject.Get()))
		{
			return false;
		}
		return !Type.IsMap()
			|| IsDeterministicTerminalType(
				Type.PinValueType.TerminalCategory,
				Type.PinValueType.TerminalSubCategoryObject.Get());
	}

	bool IsDeterministicMemberType(const FEdGraphPinType& Type)
	{
		return Type.PinSubCategoryObject.Get()
				!= FInstancedStruct::StaticStruct()
			&& IsDeterministicPinType(Type);
	}

	bool IsDiagnosticErrorPin(const UEdGraphPin& Pin)
	{
		return Pin.PinName == TEXT("OutError")
			&& Pin.PinType.PinCategory
				== UEdGraphSchema_K2::PC_String
			&& !Pin.PinType.IsContainer();
	}

	bool IsKnownUnseededRandomCall(const UFunction& Function)
	{
		static const TSet<FName> Names = {
			TEXT("RandomInteger"),
			TEXT("RandomIntegerInRange"),
			TEXT("RandomInteger64"),
			TEXT("RandomInteger64InRange"),
			TEXT("RandomBool"),
			TEXT("RandomFloat"),
			TEXT("RandomFloatInRange"),
			TEXT("RandRange"),
			TEXT("FRand"),
			TEXT("Rand"),
			TEXT("Array_Random"),
			TEXT("Array_Shuffle"),
		};
		return Names.Contains(Function.GetFName());
	}

	bool IsOrderExposingContainerCall(const UFunction& Function)
	{
		static const TSet<FName> Names = {
			TEXT("Map_Keys"),
			TEXT("Map_Values"),
			TEXT("Map_GetKeyValueByIndex"),
			TEXT("Set_ToArray"),
			TEXT("Set_GetItemByIndex"),
		};
		return Names.Contains(Function.GetFName());
	}

	bool IsGlobalGameplayTagQuery(const UFunction& Function)
	{
		if (Function.GetOwnerClass()
			!= UBlueprintGameplayTagLibrary::StaticClass())
		{
			return false;
		}

		static const TSet<FName> Names = {
			TEXT("MatchesTag"),
			TEXT("MatchesAnyTags"),
			TEXT("HasTag"),
			TEXT("HasAnyTags"),
			TEXT("HasAllTags"),
			TEXT("Filter"),
			TEXT("DoesContainerMatchTagQuery"),
		};
		return Names.Contains(Function.GetFName());
	}

	bool IsTrustedEngineValueUtility(const UFunction& Function)
	{
		const UClass* Owner = Function.GetOwnerClass();
		if (!Owner)
		{
			return false;
		}

		const FString OwnerPath = Owner->GetPathName();
		if (OwnerPath == TEXT("/Script/Engine.KismetMathLibrary"))
		{
			return Function.HasAnyFunctionFlags(FUNC_BlueprintPure);
		}
		if (OwnerPath == TEXT("/Script/Engine.KismetArrayLibrary")
			|| OwnerPath == TEXT("/Script/Engine.BlueprintSetLibrary")
			|| OwnerPath == TEXT("/Script/Engine.BlueprintMapLibrary")
			|| OwnerPath
				== TEXT("/Script/Engine.BlueprintInstancedStructLibrary")
			|| OwnerPath == TEXT("/Script/Engine.KismetNodeHelperLibrary"))
		{
			return true;
		}
		if (Owner == UBlueprintGameplayTagLibrary::StaticClass())
		{
			return Function.HasAnyFunctionFlags(FUNC_BlueprintPure)
				|| Function.GetFName() == TEXT("AddGameplayTag")
				|| Function.GetFName() == TEXT("RemoveGameplayTag")
				|| Function.GetFName()
					== TEXT("AppendGameplayTagContainers");
		}
		return false;
	}

	bool IsExplicitlyTrustedPureCall(const UFunction& Function)
	{
		const UClass* Owner = Function.GetOwnerClass();
		const bool bAnnotated =
			Function.HasMetaData(SeinDeterministicMeta)
			|| (Owner
				&& Owner->HasMetaData(SeinDeterministicMeta));
		return bAnnotated
			&& Function.HasAnyFunctionFlags(FUNC_BlueprintPure);
	}

	FString FunctionLabel(const UFunction* Function)
	{
		if (!Function)
		{
			return TEXT("<unresolved>");
		}
		const UClass* Owner = Function->GetOwnerClass();
		return FString::Printf(
			TEXT("%s::%s"),
			Owner ? *Owner->GetName() : TEXT("<unknown>"),
			*Function->GetName());
	}

	class FRecipeBlueprintAnalyzer
	{
	public:
		explicit FRecipeBlueprintAnalyzer(
			TArray<FText>& InDiagnostics)
			: Diagnostics(InDiagnostics)
		{
		}

		bool Analyze(const UBlueprint* Blueprint)
		{
			Diagnostics.Reset();
			if (!Blueprint)
			{
				AddRaw(
					LOCTEXT(
						"MissingBlueprint",
						"The canonical-state recipe Blueprint could not be resolved."));
				return false;
			}

			TSet<const UBlueprint*> VisitedBlueprints;
			for (const UBlueprint* Current = Blueprint; Current;)
			{
				if (VisitedBlueprints.Contains(Current))
				{
					AddRaw(
						FText::Format(
							LOCTEXT(
								"RecipeBlueprintInheritanceCycle",
								"Recipe Blueprint '{0}' participates in a recursive Blueprint inheritance chain. Repair the parent class before regenerating the manifest."),
							FText::FromString(
								Current->GetPathName())));
					break;
				}
				VisitedBlueprints.Add(Current);
				AnalyzeOneBlueprint(*Current);
				Current =
					UBlueprint::GetBlueprintFromClass(
						Current->ParentClass);
			}
			return Diagnostics.IsEmpty();
		}

	private:
		TArray<FText>& Diagnostics;
		TSet<const UEdGraph*> CompletedGraphs;
		TSet<const UEdGraph*> ActiveGraphs;
		TSet<const UEdGraphNode*> CompletedEventEntries;
		TSet<const UEdGraphNode*> ActiveEventEntries;
		TSet<FString> DiagnosticKeys;

		void AddRaw(const FText& Diagnostic)
		{
			const FString Key = Diagnostic.ToString();
			if (!DiagnosticKeys.Contains(Key))
			{
				DiagnosticKeys.Add(Key);
				Diagnostics.Add(Diagnostic);
			}
		}

		void AddNodeDiagnostic(
			const UEdGraphNode& Node,
			const UFunction* Function,
			const FText& Reason,
			const FText& Remediation)
		{
			const UEdGraph* Graph = Node.GetGraph();
			AddRaw(
				FText::Format(
					LOCTEXT(
						"NodeDiagnostic",
						"Graph '{0}', node '{1}', function '{2}': {3} {4}"),
					FText::FromString(
						Graph
							? Graph->GetName()
							: TEXT("<unknown>")),
					Node.GetNodeTitle(
						ENodeTitleType::ListView),
					FText::FromString(
						FunctionLabel(Function)),
					Reason,
					Remediation));
		}

		void AnalyzeOneBlueprint(const UBlueprint& Blueprint)
		{
			if (!Blueprint.IsUpToDate())
			{
				AddRaw(
					FText::Format(
						LOCTEXT(
							"BlueprintNotCompiled",
							"Recipe Blueprint '{0}' is uncompiled or has compile errors. Compile and save it before regenerating the manifest."),
						FText::FromString(
							Blueprint.GetPathName())));
				return;
			}

			for (const FBPVariableDescription& Variable :
				Blueprint.NewVariables)
			{
				if (IsDeterministicMemberType(
					Variable.VarType))
				{
					continue;
				}
				AddRaw(
					FText::Format(
						LOCTEXT(
							"UnsafeRecipeVariable",
							"Recipe Blueprint '{0}' member variable '{1}' has a non-deterministic type. Use bool/integer/name/enum or a SeinDeterministic value struct; keep world objects, floats, text, and process-local state outside the recipe."),
						FText::FromString(
							Blueprint.GetPathName()),
						FText::FromName(
							Variable.VarName)));
			}

			const FName EntryFunctions[] = {
				DeclareRecipeFunction,
				MaterializeRecipeFunction,
			};
			for (const FName EntryFunction : EntryFunctions)
			{
				for (UEdGraph* Graph : Blueprint.FunctionGraphs)
				{
					if (Graph
						&& Graph->GetFName() == EntryFunction)
					{
						AnalyzeGraph(*Graph, false);
					}
				}

				if (UK2Node_Event* Event =
					FBlueprintEditorUtils::FindOverrideForFunction(
						&Blueprint,
						USeinCanonicalStateRecipe::StaticClass(),
						EntryFunction))
				{
					AnalyzeConnectedEvent(*Event);
				}
			}
		}

		void AnalyzeConnectedEvent(UEdGraphNode& Entry)
		{
			if (CompletedEventEntries.Contains(&Entry))
			{
				return;
			}
			if (ActiveEventEntries.Contains(&Entry))
			{
				AddRaw(
					FText::Format(
						LOCTEXT(
							"RecipeEventRecursion",
							"Graph '{0}', event/helper '{1}' participates in a recursive recipe call cycle. Make the context-free recipe call graph acyclic, then compile and save it."),
						FText::FromString(
							Entry.GetGraph()
								? Entry.GetGraph()->GetName()
								: TEXT("<unknown>")),
						Entry.GetNodeTitle(
							ENodeTitleType::ListView)));
				return;
			}

			ActiveEventEntries.Add(&Entry);
			TSet<UEdGraphNode*> Visited;
			TArray<UEdGraphNode*> Queue = { &Entry };
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				UEdGraphNode* Node = Queue[Index];
				if (!Node || Visited.Contains(Node))
				{
					continue;
				}
				Visited.Add(Node);
				AnalyzeNode(*Node, false);
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						continue;
					}
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked
							&& Linked->GetOwningNode()
							&& !Visited.Contains(
								Linked->GetOwningNode()))
						{
							Queue.Add(
								Linked->GetOwningNode());
						}
					}
				}
			}
			ActiveEventEntries.Remove(&Entry);
			CompletedEventEntries.Add(&Entry);
		}

		void AnalyzeGraph(
			UEdGraph& Graph,
			bool bMacroTemplate)
		{
			if (CompletedGraphs.Contains(&Graph))
			{
				return;
			}
			if (ActiveGraphs.Contains(&Graph))
			{
				AddRaw(
					FText::Format(
						LOCTEXT(
							"RecipeGraphRecursion",
							"Graph '{0}' participates in a recursive recipe helper or macro cycle. Make the context-free recipe call graph acyclic, then compile and save it."),
						FText::FromString(
							Graph.GetName())));
				return;
			}

			ActiveGraphs.Add(&Graph);
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node)
				{
					AnalyzeNode(*Node, bMacroTemplate);
				}
			}
			ActiveGraphs.Remove(&Graph);
			CompletedGraphs.Add(&Graph);
		}

		void AnalyzeNode(
			UEdGraphNode& Node,
			bool bMacroTemplate)
		{
			if (UK2Node_Composite* Composite =
				Cast<UK2Node_Composite>(&Node))
			{
				if (!Composite->BoundGraph)
				{
					AddNodeDiagnostic(
						Node,
						nullptr,
						LOCTEXT(
							"UnresolvedComposite",
							"the collapsed helper graph cannot be resolved."),
						LOCTEXT(
							"UnresolvedCompositeFix",
							"Expand or repair the collapsed graph and compile the Blueprint."));
				}
				else
				{
					AnalyzeGraph(
						*Composite->BoundGraph,
						false);
				}
			}

			if (UK2Node_MacroInstance* Macro =
				Cast<UK2Node_MacroInstance>(&Node))
			{
				UEdGraph* MacroGraph = Macro->GetMacroGraph();
				if (!MacroGraph)
				{
					AddNodeDiagnostic(
						Node,
						nullptr,
						LOCTEXT(
							"UnresolvedMacro",
							"the macro graph cannot be resolved."),
						LOCTEXT(
							"UnresolvedMacroFix",
							"Reconnect or remove the macro and compile the Blueprint."));
				}
				else
				{
					AnalyzeGraph(*MacroGraph, true);
				}
			}

			UK2Node_CallFunction* Call =
				Cast<UK2Node_CallFunction>(&Node);
			const UFunction* Function =
				Call ? Call->GetTargetFunction() : nullptr;
			if (Call)
			{
				AnalyzeCall(*Call, Function);
			}

			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (!Pin
					|| Pin->PinType.PinCategory
						== UEdGraphSchema_K2::PC_Exec
					|| Pin->PinName
						== UEdGraphSchema_K2::PN_Self
					|| (Node.IsA<UK2Node_Event>()
						&& Pin->PinName
							== UK2Node_Event::DelegateOutputName)
					|| IsDiagnosticErrorPin(*Pin)
					|| (bMacroTemplate
						&& Pin->PinType.PinCategory
							== UEdGraphSchema_K2::PC_Wildcard))
				{
					continue;
				}
				if (!IsDeterministicPinType(Pin->PinType))
				{
					AddNodeDiagnostic(
						Node,
						Function,
						FText::Format(
							LOCTEXT(
								"UnsafePin",
								"pin '{0}' carries non-deterministic or unresolved type '{1}'."),
							FText::FromName(Pin->PinName),
							FText::FromName(
								Pin->PinType.PinCategory)),
						LOCTEXT(
							"UnsafePinFix",
							"Use fixed-point/deterministic value pins and pass all required inputs through Match Settings."));
				}
			}
		}

		void AnalyzeCall(
			UK2Node_CallFunction& Node,
			const UFunction* Function)
		{
			if (!Function)
			{
				AddNodeDiagnostic(
					Node,
					nullptr,
					LOCTEXT(
						"UnresolvedCall",
						"the call target cannot be resolved."),
					LOCTEXT(
						"UnresolvedCallFix",
						"Reconnect the function node and compile the Blueprint."));
				return;
			}
			if (Node.IsLatentFunction()
				|| Function->HasMetaData(LatentMeta))
			{
				AddNodeDiagnostic(
					Node,
					Function,
					LOCTEXT(
						"LatentCall",
						"latent execution is forbidden in a tick-zero recipe."),
					LOCTEXT(
						"LatentCallFix",
						"Replace it with synchronous local helper logic."));
				return;
			}
			if (Function->HasMetaData(WorldContextMeta)
				|| Function->HasMetaData(DefaultToSelfMeta))
			{
				AddNodeDiagnostic(
					Node,
					Function,
					LOCTEXT(
						"WorldContextCall",
						"world-context/default-to-self access is forbidden in a context-free recipe."),
					LOCTEXT(
						"WorldContextCallFix",
						"Pass the required deterministic value through Match Settings instead."));
				return;
			}
			if (IsKnownUnseededRandomCall(*Function))
			{
				AddNodeDiagnostic(
					Node,
					Function,
					LOCTEXT(
						"UnseededRandomCall",
						"unseeded random behavior is forbidden."),
					LOCTEXT(
						"UnseededRandomCallFix",
						"Use explicit deterministic inputs; if randomness is required, use a seeded Sein random state carried in those inputs."));
				return;
			}
			if (IsOrderExposingContainerCall(*Function))
			{
				AddNodeDiagnostic(
					Node,
					Function,
					LOCTEXT(
						"ContainerOrderCall",
						"the call exposes hash-container iteration order."),
					LOCTEXT(
						"ContainerOrderCallFix",
						"Materialize an explicitly sorted array before using order-sensitive results."));
				return;
			}
			if (IsGlobalGameplayTagQuery(*Function))
			{
				AddNodeDiagnostic(
					Node,
					Function,
					LOCTEXT(
						"GlobalTagQuery",
						"the call consults the process-global gameplay-tag hierarchy."),
					LOCTEXT(
						"GlobalTagQueryFix",
						"Use exact tag/name inputs or move the resolved policy into Match Settings."));
				return;
			}

			const UClass* Owner = Function->GetOwnerClass();
			if (Owner
				&& (Owner == USeinCanonicalStateRecipe::StaticClass()
					|| Owner->IsChildOf(
						USeinCanonicalStateRecipe::StaticClass())))
			{
				if (UBlueprint* OwnerBlueprint =
					UBlueprint::GetBlueprintFromClass(Owner))
				{
					AnalyzeBlueprintHelperCall(
						Node,
						*Function,
						*OwnerBlueprint,
						LOCTEXT(
							"RecipeHelperKind",
							"recipe helper"));
					return;
				}

				if (Owner
					== USeinCanonicalStateRecipe::StaticClass()
					&& (Function->GetFName()
							== DeclareRecipeFunction
						|| Function->GetFName()
							== MaterializeRecipeFunction))
				{
					return;
				}
			}

			// A pure Blueprint Function Library graph remains fully
			// Blueprint-authorable and composable: follow its implementation
			// recursively instead of requiring native metadata.
			if (Owner
				&& Owner->IsChildOf(
					UBlueprintFunctionLibrary::StaticClass())
				&& Function->HasAnyFunctionFlags(
					FUNC_BlueprintPure))
			{
				if (UBlueprint* OwnerBlueprint =
					UBlueprint::GetBlueprintFromClass(Owner))
				{
					AnalyzeBlueprintHelperCall(
						Node,
						*Function,
						*OwnerBlueprint,
						LOCTEXT(
							"BlueprintUtilityKind",
							"pure Blueprint utility"));
					return;
				}
			}

			if (IsExplicitlyTrustedPureCall(*Function)
				|| IsTrustedEngineValueUtility(*Function))
			{
				return;
			}

			AddNodeDiagnostic(
				Node,
				Function,
				LOCTEXT(
					"UntrustedExternalCall",
					"the external call has no context-free deterministic contract."),
				LOCTEXT(
					"UntrustedExternalCallFix",
					"Use local recipe helpers, or mark a proven pure native function/owner with SeinDeterministic metadata."));
		}

		void AnalyzeBlueprintHelperCall(
			UK2Node_CallFunction& Node,
			const UFunction& Function,
			const UBlueprint& OwnerBlueprint,
			const FText& HelperKind)
		{
			if (!OwnerBlueprint.IsUpToDate())
			{
				AddNodeDiagnostic(
					Node,
					&Function,
					FText::Format(
						LOCTEXT(
							"UncompiledBlueprintHelper",
							"the owning {0} Blueprint is uncompiled or has compile errors."),
						HelperKind),
					LOCTEXT(
						"UncompiledBlueprintHelperFix",
						"Compile and save the utility Blueprint, then reconnect the call."));
				return;
			}

			const UEdGraphNode* EntryNode = nullptr;
			UEdGraph* FunctionGraph =
				Node.GetFunctionGraph(EntryNode);
			if (!FunctionGraph)
			{
				AddNodeDiagnostic(
					Node,
					&Function,
					FText::Format(
						LOCTEXT(
							"UnresolvedBlueprintHelper",
							"the {0} graph cannot be resolved."),
						HelperKind),
					LOCTEXT(
						"UnresolvedBlueprintHelperFix",
						"Compile the owning Blueprint and reconnect the call."));
				return;
			}
			if (EntryNode)
			{
				AnalyzeConnectedEvent(
					*const_cast<UEdGraphNode*>(
						EntryNode));
			}
			else
			{
				AnalyzeGraph(*FunctionGraph, false);
			}
		}
	};
}

bool SeinContextFreeRecipeDeterminism::ValidateClass(
	const UClass* RecipeClass,
	TArray<FText>& OutDiagnostics)
{
	OutDiagnostics.Reset();
	if (!RecipeClass
		|| !RecipeClass->IsChildOf(
			USeinCanonicalStateRecipe::StaticClass()))
	{
		OutDiagnostics.Add(
			LOCTEXT(
				"InvalidRecipeClass",
				"The configured class is missing or is not a canonical-state recipe."));
		return false;
	}
	if (const UBlueprint* Blueprint =
		UBlueprint::GetBlueprintFromClass(RecipeClass))
	{
		return ValidateBlueprint(
			Blueprint,
			OutDiagnostics);
	}
	return true;
}

bool SeinContextFreeRecipeDeterminism::ValidateBlueprint(
	const UBlueprint* Blueprint,
	TArray<FText>& OutDiagnostics)
{
	FRecipeBlueprintAnalyzer Analyzer(OutDiagnostics);
	return Analyzer.Analyze(Blueprint);
}

#undef LOCTEXT_NAMESPACE
