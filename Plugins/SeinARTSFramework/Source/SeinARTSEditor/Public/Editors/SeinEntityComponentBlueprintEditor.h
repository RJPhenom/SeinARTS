/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponentBlueprintEditor.h
 * @date:    9/1/2026
 * @author:  RJ Macklem
 * @brief:   Dedicated data-only editor for Sein entity component Blueprints
 *           (USeinEntityComponentBlueprint): a variables panel plus a Class
 *           Defaults panel, deliberately WITHOUT graph surfaces — the
 *           UserDefinedStruct editor's shape applied to a Blueprint class.
 *
 *           Variable rows drive the engine's own Blueprint-variable API
 *           (FBlueprintEditorUtils Add/Rename/ChangeType/Remove), and the
 *           type picker is filtered to the SeinDeterminism whitelist AT
 *           SELECTION TIME — something the stock UDS editor cannot do (its
 *           validator strips after the fact). Every variable is marked
 *           instance-editable on creation so per-placed-actor overrides work.
 *           Edits recompile the Blueprint, which triggers the payload-UDS
 *           auto-sync and the class-default bake downstream.
 */

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/WeakObjectPtr.h"

class IDetailsView;
class SWidget;
class UBlueprint;
struct FBPVariableDescription;
struct FEdGraphPinType;

class SEINARTSEDITOR_API FSeinEntityComponentBlueprintEditor
	: public FAssetEditorToolkit
	, public FGCObject
{
public:
	void InitEditor(const EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UBlueprint* InBlueprint);

	// FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FSeinEntityComponentBlueprintEditor");
	}

private:
	TSharedRef<SDockTab> SpawnVariablesTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDefaultsTab(const FSpawnTabArgs& Args);

	/** Rebuild the variables list widget from the Blueprint's NewVariables. */
	void RefreshVariablesPanel();
	/** Point the defaults view at the current generated-class CDO. */
	void RefreshDefaultsView();
	/** Compile after a variable change, then refresh both panels. */
	void CompileAndRefresh();

	void OnAddVariable();
	void OnRemoveVariable(FGuid VarGuid);
	void OnRenameVariable(FGuid VarGuid, const FText& NewName);
	void OnVariableTypeChanged(FGuid VarGuid, const FEdGraphPinType& NewType);

	/** One row per Blueprint variable. */
	TSharedRef<SWidget> MakeVariableRow(const FBPVariableDescription& Variable);

	TWeakObjectPtr<UBlueprint> Blueprint;
	TSharedPtr<IDetailsView> DefaultsView;
	TSharedPtr<class SVerticalBox> VariablesBox;
	bool bCompiling = false;

	static const FName VariablesTabId;
	static const FName DefaultsTabId;
};
