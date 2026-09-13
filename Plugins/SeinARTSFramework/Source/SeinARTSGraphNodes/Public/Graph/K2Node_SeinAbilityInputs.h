/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         K2Node_SeinAbilityInputs.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Typed Blueprint activation pins derived from ability variables.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_SeinAbilityInputs.generated.h"

UCLASS()
class SEINARTSGRAPHNODES_API UK2Node_SeinAbilityInputs : public UK2Node
{
	GENERATED_BODY()
public:
	/** 0 builds inputs, 1 submits player input, 2 chains simulation input. */
	UPROPERTY()
	uint8 Operation = 1;
	UPROPERTY()
	TObjectPtr<UClass> AbilityClass;
	UPROPERTY()
	FGuid InputSchema;
	bool RefreshInputPins();
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewIndex, const UEdGraphPin* OldPin, int32 OldIndex) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetTooltipText() const override;
	virtual bool IsNodePure() const override { return Operation == 0; }
	virtual FBlueprintNodeSignature GetSignature() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& Compiler, UEdGraph* Graph) override;
	virtual bool HasExternalDependencies(TArray<UStruct*>* Output = nullptr) const override;
};
