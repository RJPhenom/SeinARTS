/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementTuningExport.cpp
 */

#include "Util/SeinMovementTuningExport.h"
#include "Util/SeinDeterminismRules.h"
#include "Factories/SeinSimComponentFactory.h"  // meta-key constants

#include "Engine/Blueprint.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"  // FStructVariableDescription (full definition)
#include "Kismet2/BlueprintEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMovementTuning, Log, All);

namespace SeinMovementTuning
{

namespace
{
	/** One BP tuning variable, reduced to what the UDS field needs. */
	struct FDesiredField
	{
		FString          Name;          // == BP var name; becomes the UDS field's friendly name
		FEdGraphPinType  PinType;
		FString          DefaultValue;
	};

	/** USeinMovement, resolved by path so this module needs no Movement link dependency. */
	UClass* GetSeinMovementClass()
	{
		static const TCHAR* Path = TEXT("/Script/SeinARTSMovement.SeinMovement");
		return FindObject<UClass>(nullptr, Path);
	}

	/** The CDO's TuningStruct object-property, found by reflection (no Movement type needed). */
	FObjectProperty* FindTuningStructProperty(const UClass* GeneratedClass)
	{
		if (!GeneratedClass) return nullptr;
		return CastField<FObjectProperty>(GeneratedClass->FindPropertyByName(TEXT("TuningStruct")));
	}

	/** Currently-linked tuning UDS (from the CDO), or the on-disk "<BPName>TuningData"
	 *  asset if one already exists. Null if neither — caller creates one. */
	UUserDefinedStruct* ResolveExistingTuningUDS(UBlueprint* Blueprint)
	{
		UClass* GenClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
		if (GenClass)
		{
			if (UObject* CDO = GenClass->GetDefaultObject(/*bCreateIfNeeded*/ false))
			{
				if (FObjectProperty* Prop = FindTuningStructProperty(GenClass))
				{
					if (UUserDefinedStruct* Linked = Cast<UUserDefinedStruct>(Prop->GetObjectPropertyValue_InContainer(CDO)))
					{
						return Linked;
					}
				}
			}
		}

		// Fall back to the conventional sibling asset path (loads from disk if present).
		const FString PkgDir   = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName());
		const FString AssetNm  = Blueprint->GetName() + TEXT("TuningData");
		const FString ObjPath  = PkgDir / (AssetNm + TEXT(".") + AssetNm);
		return LoadObject<UUserDefinedStruct>(nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Create the paired "<BPName>TuningData" UDS asset next to the Blueprint, tagged
	 *  SeinDeterministic + SeinSubData (so the determinism validator guards it and it
	 *  surfaces only in the sub-data picker, mirroring FSein...MovementData). */
	UUserDefinedStruct* CreateTuningUDS(UBlueprint* Blueprint)
	{
		const FString PkgDir  = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName());
		const FString AssetNm = Blueprint->GetName() + TEXT("TuningData");
		const FString PkgName = PkgDir / AssetNm;

		UPackage* Package = CreatePackage(*PkgName);
		if (!Package) return nullptr;

		UUserDefinedStruct* UDS = FStructureEditorUtils::CreateUserDefinedStruct(
			Package, FName(*AssetNm), RF_Public | RF_Standalone | RF_Transactional);
		if (!UDS) return nullptr;

		UDS->SetMetaData(USeinSimComponentFactory::SeinDeterministicMetaKey, TEXT("true"));
		UDS->SetMetaData(USeinSimComponentFactory::SeinSubDataMetaKey, TEXT("true"));

		FAssetRegistryModule::AssetCreated(UDS);
		Package->MarkPackageDirty();
		return UDS;
	}

	/** Rename/relocate the UDS asset (package + object, with redirector fix-up) so it tracks
	 *  the BP's current name + folder. Best-effort: on failure the struct keeps its name and
	 *  the link stays valid. Also migrates the legacy "<Name>_Tuning" asset to "<Name>TuningData". */
	void RenameTuningUDS(UUserDefinedStruct* UDS, const FString& NewPackagePath, const FString& NewName)
	{
		if (!UDS) return;
		const FString CurPath = FPackageName::GetLongPackagePath(UDS->GetOutermost()->GetName());
		if (UDS->GetName() == NewName && CurPath == NewPackagePath) return;  // already correct

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<FAssetRenameData> Renames;
		Renames.Add(FAssetRenameData(UDS, NewPackagePath, NewName));
		AssetToolsModule.Get().RenameAssets(Renames);
	}

	/** Add/remove/retype the UDS's fields to match `Desired`, keyed by friendly name.
	 *  Order: reconcile desired first (so the UDS is never transiently empty), then drop
	 *  strays (including the dummy field CreateUserDefinedStruct seeds). */
	void SyncFields(UUserDefinedStruct* UDS, const TArray<FDesiredField>& Desired)
	{
		if (!UDS) return;

		// Pass 1 — ensure each desired field exists with the right type.
		for (const FDesiredField& D : Desired)
		{
			FGuid ExistingGuid;
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				if (V.FriendlyName == D.Name) { ExistingGuid = V.VarGuid; break; }
			}

			if (ExistingGuid.IsValid())
			{
				if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, ExistingGuid))
				{
					if (VD->ToPinType() != D.PinType)
					{
						FStructureEditorUtils::ChangeVariableType(UDS, ExistingGuid, D.PinType);
					}
				}
				continue;
			}

			// New field: AddVariable auto-names it, so snapshot GUIDs to find the new one,
			// then rename to the BP var name and apply the default (best-effort).
			TSet<FGuid> Before;
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS)) { Before.Add(V.VarGuid); }
			if (!FStructureEditorUtils::AddVariable(UDS, D.PinType)) { continue; }

			FGuid NewGuid;
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				if (!Before.Contains(V.VarGuid)) { NewGuid = V.VarGuid; break; }
			}
			if (NewGuid.IsValid())
			{
				FStructureEditorUtils::RenameVariable(UDS, NewGuid, D.Name);
				if (!D.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(UDS, NewGuid, D.DefaultValue);
				}
			}
		}

		// Pass 2 — drop any field not in Desired (snapshot GUIDs first; Remove mutates the list).
		TSet<FString> DesiredNames;
		for (const FDesiredField& D : Desired) { DesiredNames.Add(D.Name); }

		TArray<FGuid> ToRemove;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
		{
			if (!DesiredNames.Contains(V.FriendlyName)) { ToRemove.Add(V.VarGuid); }
		}
		for (const FGuid& G : ToRemove) { FStructureEditorUtils::RemoveVariable(UDS, G); }
	}

	/** Point the BP CDO's TuningStruct at `Struct` (null clears) and dirty the BP. */
	void StampTuningStruct(UBlueprint* Blueprint, UScriptStruct* Struct)
	{
		UClass* GenClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
		if (!GenClass) return;
		UObject* CDO = GenClass->GetDefaultObject(/*bCreateIfNeeded*/ false);
		FObjectProperty* Prop = FindTuningStructProperty(GenClass);
		if (!CDO || !Prop) return;
		Prop->SetObjectPropertyValue_InContainer(CDO, Struct);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

bool IsMovementModeBlueprint(const UBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GeneratedClass) return false;
	const UClass* Base = GetSeinMovementClass();
	return Base && Blueprint->GeneratedClass->IsChildOf(Base);
}

UUserDefinedStruct* SyncTuningStructForBlueprint(UBlueprint* Blueprint)
{
	if (!IsMovementModeBlueprint(Blueprint))
	{
		UE_LOG(LogSeinMovementTuning, Verbose, TEXT("SyncTuningStruct: %s is not a movement-mode Blueprint."),
			Blueprint ? *Blueprint->GetName() : TEXT("<null>"));
		return nullptr;
	}

	// Gather tuning vars: Instance-Editable AND deterministic-typed NewVariables. Instance-Editable
	// is the designer's intent signal — "expose this knob per-unit" — which cleanly separates tuning
	// from internal scratch state (un-exposed vars stay out of the struct). Deterministic-typed keeps
	// the sim safe. A var that doesn't appear was almost certainly not marked Instance Editable (eye).
	TArray<FDesiredField> Desired;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		const bool bInstanceEditable = (Var.PropertyFlags & CPF_Edit) && !(Var.PropertyFlags & CPF_DisableEditOnInstance);
		if (!bInstanceEditable) continue;
		if (!SeinDeterminism::IsPinTypeDeterministic(Var.VarType)) continue;
		Desired.Add({ Var.VarName.ToString(), Var.VarType, Var.DefaultValue });
	}

	// No tuning vars → unlink and make no asset.
	if (Desired.Num() == 0)
	{
		StampTuningStruct(Blueprint, nullptr);
		UE_LOG(LogSeinMovementTuning, Log, TEXT("SyncTuningStruct: %s has no tuning vars; cleared TuningStruct."),
			*Blueprint->GetName());
		return nullptr;
	}

	UUserDefinedStruct* UDS = ResolveExistingTuningUDS(Blueprint);
	if (UDS)
	{
		// Track BP renames / folder-moves (and migrate the legacy "<Name>_Tuning" asset name) by
		// renaming the existing struct to match the BP — SyncFields only touches fields, not the name.
		const FString BPPath = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName());
		RenameTuningUDS(UDS, BPPath, Blueprint->GetName() + TEXT("TuningData"));
	}
	else
	{
		UDS = CreateTuningUDS(Blueprint);
	}
	if (!UDS)
	{
		UE_LOG(LogSeinMovementTuning, Warning, TEXT("SyncTuningStruct: failed to get/create tuning UDS for %s."),
			*Blueprint->GetName());
		return nullptr;
	}

	SyncFields(UDS, Desired);
	FStructureEditorUtils::OnStructureChanged(UDS, FStructureEditorUtils::Unknown);
	StampTuningStruct(Blueprint, UDS);

	UE_LOG(LogSeinMovementTuning, Log, TEXT("SyncTuningStruct: %s -> %s (%d field(s))."),
		*Blueprint->GetName(), *UDS->GetName(), Desired.Num());
	return UDS;
}

}  // namespace SeinMovementTuning
