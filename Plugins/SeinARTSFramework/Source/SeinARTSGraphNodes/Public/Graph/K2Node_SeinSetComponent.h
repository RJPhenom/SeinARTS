/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    K2Node_SeinSetComponent.h
 * @brief:   Blueprint editor node for typed component writes. Symmetric to
 *           UK2Node_SeinGetComponent — designer picks a struct type from the
 *           action menu and gets back a "Set <StructName> Data" node with a
 *           pre-typed input pin (Make-Struct compatible) that writes into
 *           deterministic component storage at runtime.
 *
 *           Wraps `USeinComponentBPFL::SeinSetComponentTyped` (CustomThunk).
 */

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_SeinSetComponent.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

UCLASS()
class SEINARTSGRAPHNODES_API UK2Node_SeinSetComponent : public UK2Node
{
	GENERATED_BODY()

public:
	/** Struct type the user picked from the action menu — types the input pin. */
	UPROPERTY()
	TObjectPtr<UScriptStruct> SelectedStruct = nullptr;

	// ===== UEdGraphNode =====
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetMenuCategory() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FText GetTooltipText() const override;

	// ===== UK2Node =====
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsNodePure() const override { return false; }

	// ===== Pin accessors =====
	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetWorldContextPin() const;
	UEdGraphPin* GetEntityHandlePin() const;
	UEdGraphPin* GetInStructPin() const;
	UEdGraphPin* GetSuccessPin() const;
};
