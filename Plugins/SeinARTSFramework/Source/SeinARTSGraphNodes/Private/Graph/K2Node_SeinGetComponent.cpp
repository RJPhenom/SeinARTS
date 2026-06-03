/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    K2Node_SeinGetComponent.cpp
 */

#include "Graph/K2Node_SeinGetComponent.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/UObjectIterator.h"
#include "StructUtils/UserDefinedStruct.h"

#include "Components/SeinComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Lib/SeinComponentBPFL.h"

// Inlined SeinDeterministic meta key — same name the factory uses for UDS
// tagging. K2 nodes can't dep on SeinARTSEditor (Editor module type) because
// they live in an UncookedOnly module, so we duplicate the constant. If the
// factory's key ever changes, this needs to stay in sync.
namespace { static const FName GSeinDeterministicMetaKey(TEXT("SeinDeterministic")); }

#define LOCTEXT_NAMESPACE "K2Node_SeinGetComponent"

namespace SeinK2GetCompLocal
{
	static const FName PN_WorldContext(TEXT("WorldContextObject"));
	static const FName PN_EntityHandle(TEXT("EntityHandle"));
	static const FName PN_OutStruct(TEXT("OutStruct"));
	static const FName PN_Success(TEXT("ReturnValue"));

	/** Discover all FSeinComponent-derived structs the user might want to read.
	 *  Walks every native UScriptStruct (filtered by IsChildOf) plus every
	 *  UDS whose meta carries `SeinDeterministic` (UDS can't IsChildOf reliably —
	 *  see comments in FSeinInstancedStructFilter). One menu action per unique
	 *  type, sorted stable by name so the action menu stays consistent across
	 *  reloads. */
	static void GatherCandidateStructs(TArray<UScriptStruct*>& Out)
	{
		const UScriptStruct* Base = FSeinComponent::StaticStruct();
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* S = *It;
			if (!S || S == Base) continue;

			// Native USTRUCT derived from FSeinComponent.
			if (S->IsChildOf(Base))
			{
				Out.Add(S);
				continue;
			}

			// UDS path: IsChildOf is unreliable for UDS (the compiler clears
			// SuperStruct), so gate via the SeinDeterministic meta key the
			// factory stamps at UDS creation.
			if (S->IsA<UUserDefinedStruct>() && S->HasMetaData(GSeinDeterministicMetaKey))
			{
				Out.Add(S);
			}
		}

		Out.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetFName().LexicalLess(B.GetFName());
		});
	}

	/** Strip leading "FSein" / "F" from a struct name and append " Data" if
	 *  not already present — produces consistent menu labels like
	 *  "Get Combat Data", "Get Movement Data". */
	static FString PrettyName(const UScriptStruct* S)
	{
		if (!S) return FString();
		FString N = S->GetName();
		N.RemoveFromStart(TEXT("Sein"));
		if (N.StartsWith(TEXT("F"))) N.RemoveAt(0);
		if (!N.EndsWith(TEXT("Data"))) N += TEXT(" Data");
		else
		{
			// Insert space before "Data" if missing — "CombatData" → "Combat Data".
			const int32 Idx = N.Len() - 4;
			if (Idx > 0 && N[Idx - 1] != TEXT(' '))
			{
				N.InsertAt(Idx, TEXT(' '));
			}
		}
		return N;
	}
}

// =============================================================================
// Pin layout
// =============================================================================

void UK2Node_SeinGetComponent::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	// Exec in / out — node is impure so it must thread the execution chain.
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// World context — UObject ref, hidden if the graph's owner provides Self.
	UEdGraphPin* WorldPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object,
		UObject::StaticClass(), SeinK2GetCompLocal::PN_WorldContext);
	WorldPin->bHidden = true; // Auto-wired by K2 compiler when owner is a UObject.

	// Entity handle input — typed to the FSeinEntityHandle struct.
	UEdGraphPin* HandlePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct,
		FSeinEntityHandle::StaticStruct(), SeinK2GetCompLocal::PN_EntityHandle);
	HandlePin->PinFriendlyName = LOCTEXT("EntityHandlePinName", "Entity");

	// Typed output struct pin — type derives from SelectedStruct. Falls back
	// to a wildcard if no struct has been picked yet (action-menu spawn always
	// sets it, but pre-pick nodes loaded from old saves get a placeholder).
	if (SelectedStruct)
	{
		UEdGraphPin* OutPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct,
			SelectedStruct, SeinK2GetCompLocal::PN_OutStruct);
		OutPin->PinFriendlyName = FText::FromString(SeinK2GetCompLocal::PrettyName(SelectedStruct));
	}
	else
	{
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, nullptr,
			SeinK2GetCompLocal::PN_OutStruct);
	}

	// Bool success — false if the entity doesn't carry this component yet.
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, SeinK2GetCompLocal::PN_Success);
}

UEdGraphPin* UK2Node_SeinGetComponent::GetExecPin() const
	{ return FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input); }
UEdGraphPin* UK2Node_SeinGetComponent::GetThenPin() const
	{ return FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output); }
UEdGraphPin* UK2Node_SeinGetComponent::GetWorldContextPin() const
	{ return FindPinChecked(SeinK2GetCompLocal::PN_WorldContext, EGPD_Input); }
UEdGraphPin* UK2Node_SeinGetComponent::GetEntityHandlePin() const
	{ return FindPinChecked(SeinK2GetCompLocal::PN_EntityHandle, EGPD_Input); }
UEdGraphPin* UK2Node_SeinGetComponent::GetOutStructPin() const
	{ return FindPinChecked(SeinK2GetCompLocal::PN_OutStruct, EGPD_Output); }
UEdGraphPin* UK2Node_SeinGetComponent::GetSuccessPin() const
	{ return FindPinChecked(SeinK2GetCompLocal::PN_Success, EGPD_Output); }

// =============================================================================
// Titles, category, icon
// =============================================================================

FText UK2Node_SeinGetComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!SelectedStruct)
	{
		return LOCTEXT("NodeTitle_NoStruct", "Get Sein Component (no struct)");
	}
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		return FText::Format(LOCTEXT("NodeMenuTitle", "Get {0}"),
			FText::FromString(SeinK2GetCompLocal::PrettyName(SelectedStruct)));
	}
	return FText::Format(LOCTEXT("NodeFullTitle", "Get {0}"),
		FText::FromString(SeinK2GetCompLocal::PrettyName(SelectedStruct)));
}

FText UK2Node_SeinGetComponent::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "SeinARTS|Component");
}

FSlateIcon UK2Node_SeinGetComponent::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIconFinder::FindIconForClass(UScriptStruct::StaticClass());
}

FText UK2Node_SeinGetComponent::GetTooltipText() const
{
	if (!SelectedStruct)
	{
		return LOCTEXT("Tooltip_NoStruct",
			"Read a Sein sim component from deterministic storage. Pick a struct from the action menu to type the output pin.");
	}
	return FText::Format(LOCTEXT("Tooltip_WithStruct",
		"Read this entity's {0} component from deterministic storage. Returns false if the entity doesn't carry it."),
		FText::FromString(SeinK2GetCompLocal::PrettyName(SelectedStruct)));
}

// =============================================================================
// Action menu — one entry per discovered FSeinComponent substruct
// =============================================================================

void UK2Node_SeinGetComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// ActionRegistrar guards against duplicate registration per node class —
	// the action-key is the class CDO. We register one Spawner per candidate
	// struct, with a node-customization lambda that sets SelectedStruct.
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	TArray<UScriptStruct*> Candidates;
	SeinK2GetCompLocal::GatherCandidateStructs(Candidates);

	for (UScriptStruct* S : Candidates)
	{
		if (!S) continue;

		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		if (!Spawner) continue;

		// Customization fires when the user drops the action — set the
		// selected struct before AllocateDefaultPins runs.
		TWeakObjectPtr<UScriptStruct> WeakStruct = S;
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[WeakStruct](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
			{
				if (UK2Node_SeinGetComponent* TypedNode = Cast<UK2Node_SeinGetComponent>(NewNode))
				{
					TypedNode->SelectedStruct = WeakStruct.Get();
				}
			});

		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

// Match pins across reconstruction by name — SelectedStruct change is rare
// (one-shot at spawn) and the default name-based match is the right behavior.
UK2Node::ERedirectType UK2Node_SeinGetComponent::DoPinsMatchForReconstruction(
	const UEdGraphPin* NewPin, int32 NewPinIndex,
	const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	return Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
}

// =============================================================================
// Expansion — emit a CallFunction node bound to SeinGetComponentTyped
// =============================================================================
//
// The wildcard struct param on the BPFL is the bridge: the K2 compiler sees
// our typed OutStruct pin connected to the CallFunction's wildcard parameter
// and stamps the right struct type into the script-frame property at compile
// time. The CustomThunk we wrote in SeinComponentBPFL.cpp reads that property
// at execution time and copies storage bytes into the BP buffer.
void UK2Node_SeinGetComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!SelectedStruct)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("ExpandNode_NoStruct",
			"@@ has no selected struct — re-spawn the node from the SeinARTS|Component menu.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(USeinComponentBPFL, SeinGetComponentTyped),
		USeinComponentBPFL::StaticClass());
	CallNode->AllocateDefaultPins();

	// Move exec + then.
	CompilerContext.MovePinLinksToIntermediate(
		*GetExecPin(),
		*CallNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(
		*GetThenPin(),
		*CallNode->GetThenPin());

	// Forward world context (auto-resolved by the K2 compiler when WorldContextObject
	// is hidden; our hidden pin maps directly to the BPFL's hidden world context).
	if (UEdGraphPin* CallWorldPin = CallNode->FindPin(SeinK2GetCompLocal::PN_WorldContext))
	{
		CompilerContext.MovePinLinksToIntermediate(*GetWorldContextPin(), *CallWorldPin);
	}

	// Entity handle passthrough.
	if (UEdGraphPin* CallHandlePin = CallNode->FindPin(SeinK2GetCompLocal::PN_EntityHandle))
	{
		CompilerContext.MovePinLinksToIntermediate(*GetEntityHandlePin(), *CallHandlePin);
	}

	// OutStruct pin on the call is the wildcard — set its type to SelectedStruct
	// so the K2 compiler stamps the right script-frame property, then move our
	// typed output pin's links onto it.
	if (UEdGraphPin* CallOutStructPin = CallNode->FindPin(SeinK2GetCompLocal::PN_OutStruct))
	{
		CallOutStructPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		CallOutStructPin->PinType.PinSubCategoryObject = SelectedStruct;
		CompilerContext.MovePinLinksToIntermediate(*GetOutStructPin(), *CallOutStructPin);
	}

	// Return value (success bool).
	if (UEdGraphPin* CallReturnPin = CallNode->GetReturnValuePin())
	{
		CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
