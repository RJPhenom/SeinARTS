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
	/** UDS-field metadata key storing the source BP variable's GUID. Lets a renamed tuning var map to
	 *  its existing UDS field (preserving per-unit authored values — UDS instances serialize by field
	 *  GUID) instead of being dropped and re-added under the new name. */
	const FName SeinSourceVarGuidKey(TEXT("SeinSourceVarGuid"));

	/** Display/validation metadata copied from the BP tuning variable to its UDS field, so the per-unit
	 *  MovementClassData editor shows the same tooltip, clamps, slider range, units, and grouping. */
	const TArray<FName>& TuningMetaKeys()
	{
		static const TArray<FName> Keys = {
			TEXT("tooltip"), TEXT("ClampMin"), TEXT("ClampMax"),
			TEXT("UIMin"), TEXT("UIMax"), TEXT("Units"), TEXT("Category")
		};
		return Keys;
	}

	/** One BP tuning variable, reduced to what the UDS field needs. */
	struct FDesiredField
	{
		FString               Name;          // == BP var name; becomes the UDS field's friendly name
		FEdGraphPinType       PinType;
		FString               DefaultValue;
		FGuid                 SourceGuid;     // source BP variable's stable GUID — the rename-tracking key
		TMap<FName, FString>  Meta;           // whitelisted display/validation metadata from the BP var
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

		USeinSimComponentFactory::MarkUserDefinedStructAsSubData(UDS);

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

	/** Stamp the source-var GUID, re-sync the default value from the BP var, and propagate the
	 *  whitelisted display/validation metadata onto an existing UDS field (clearing any the BP var no
	 *  longer carries). The BP var is the authoritative source for a tool-managed UDS, so the default
	 *  is re-applied every pass — only when it actually changed, to avoid needless struct rebuilds. */
	void ApplyFieldMetaAndDefault(UUserDefinedStruct* UDS, const FGuid& FieldGuid, const FDesiredField& D)
	{
		FStructureEditorUtils::SetMetaData(UDS, FieldGuid, SeinSourceVarGuidKey, D.SourceGuid.ToString());

		if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, FieldGuid))
		{
			if (VD->DefaultValue != D.DefaultValue)
			{
				FStructureEditorUtils::ChangeVariableDefaultValue(UDS, FieldGuid, D.DefaultValue);
			}
		}

		for (const FName& Key : TuningMetaKeys())
		{
			if (const FString* Value = D.Meta.Find(Key))
			{
				FStructureEditorUtils::SetMetaData(UDS, FieldGuid, Key, *Value);
			}
			else if (const FString* Existing = FStructureEditorUtils::GetMetaData(UDS, FieldGuid, Key))
			{
				if (!Existing->IsEmpty()) { FStructureEditorUtils::SetMetaData(UDS, FieldGuid, Key, FString()); }
			}
		}
	}

	/** Reconcile the UDS's fields to `Desired`, matching by the SOURCE BP-VAR GUID stamped in each
	 *  field's metadata (not by name). So renaming a tuning variable RENAMES its UDS field — which
	 *  preserves every per-unit authored value, because UDS instances serialize by the field's own
	 *  GUID, which a rename keeps — instead of dropping the field and re-adding it under the new name.
	 *  Legacy fields predating the GUID stamp are adopted by friendly name on the first sync.
	 *  Order: reconcile desired first (so the UDS is never transiently empty), then drop strays. */
	void SyncFields(UUserDefinedStruct* UDS, const TArray<FDesiredField>& Desired)
	{
		if (!UDS) return;

		// Find an existing field for a desired var: by stamped source GUID (rename-stable), else by
		// friendly name (legacy field, adopted below). Returns the UDS field's own GUID.
		auto FindField = [UDS](const FDesiredField& D, FGuid& OutFieldGuid) -> bool
		{
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				const FString* Stored = FStructureEditorUtils::GetMetaData(UDS, V.VarGuid, SeinSourceVarGuidKey);
				if (Stored && *Stored == D.SourceGuid.ToString()) { OutFieldGuid = V.VarGuid; return true; }
			}
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				if (V.FriendlyName == D.Name) { OutFieldGuid = V.VarGuid; return true; }
			}
			return false;
		};

		// Pass 1 — ensure each desired field exists; reconcile its name + type, and (re)stamp its
		// source-var GUID so it tracks renames from here on.
		for (const FDesiredField& D : Desired)
		{
			FGuid FieldGuid;
			if (FindField(D, FieldGuid))
			{
				if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, FieldGuid))
				{
					if (VD->FriendlyName != D.Name)   { FStructureEditorUtils::RenameVariable(UDS, FieldGuid, D.Name); }
					if (VD->ToPinType()  != D.PinType) { FStructureEditorUtils::ChangeVariableType(UDS, FieldGuid, D.PinType); }
				}
				ApplyFieldMetaAndDefault(UDS, FieldGuid, D);
				continue;
			}

			// New field: AddVariable auto-names it, so snapshot GUIDs to find the new one, then rename
			// to the BP var name, stamp its source GUID, and apply the default (best-effort).
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
				ApplyFieldMetaAndDefault(UDS, NewGuid, D);
			}
		}

		// Pass 2 — drop fields whose source var is gone. Keep a field iff its stamped source GUID is
		// still desired (or, for an un-adopted legacy field with no stamp, its name is). Snapshot
		// GUIDs first; Remove mutates the list.
		TSet<FString> DesiredGuids;
		TSet<FString> DesiredNames;
		for (const FDesiredField& D : Desired) { DesiredGuids.Add(D.SourceGuid.ToString()); DesiredNames.Add(D.Name); }

		TArray<FGuid> ToRemove;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
		{
			const FString* Stored = FStructureEditorUtils::GetMetaData(UDS, V.VarGuid, SeinSourceVarGuidKey);
			const bool bKeep = Stored ? DesiredGuids.Contains(*Stored) : DesiredNames.Contains(V.FriendlyName);
			if (!bKeep) { ToRemove.Add(V.VarGuid); }
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

		FDesiredField Field;
		Field.Name         = Var.VarName.ToString();
		Field.PinType      = Var.VarType;
		Field.DefaultValue = Var.DefaultValue;
		Field.SourceGuid   = Var.VarGuid;
		for (const FName& Key : TuningMetaKeys())
		{
			if (Var.HasMetaData(Key)) { Field.Meta.Add(Key, Var.GetMetaData(Key)); }
		}
		Desired.Add(MoveTemp(Field));
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

	// Reapply the durable marker on every sync so assets created before the
	// editor-data fix migrate the next time their tuning struct is reconciled.
	USeinSimComponentFactory::MarkUserDefinedStructAsSubData(UDS);
	SyncFields(UDS, Desired);
	FStructureEditorUtils::OnStructureChanged(UDS, FStructureEditorUtils::Unknown);
	StampTuningStruct(Blueprint, UDS);

	UE_LOG(LogSeinMovementTuning, Log, TEXT("SyncTuningStruct: %s -> %s (%d field(s))."),
		*Blueprint->GetName(), *UDS->GetName(), Desired.Num());
	return UDS;
}

}  // namespace SeinMovementTuning
