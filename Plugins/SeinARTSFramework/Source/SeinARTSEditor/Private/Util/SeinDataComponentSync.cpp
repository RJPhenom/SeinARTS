/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDataComponentSync.cpp
 */

#include "Util/SeinDataComponentSync.h"
#include "Util/SeinDeterminismRules.h"
#include "Factories/SeinSimComponentFactory.h"

#include "Authoring/SeinDataComponent.h"
#include "Engine/Blueprint.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinDataComponentSync, Log, All);

namespace SeinDataComponentSync
{

namespace
{
	/** Same rename-tracking key the movement tuning export uses: UDS-field
	 *  metadata storing the source BP variable's GUID, so a renamed variable
	 *  RENAMES its field (preserving authored values — UDS instances
	 *  serialize by field GUID) instead of drop-and-re-add. */
	const FName SeinSourceVarGuidKey(TEXT("SeinSourceVarGuid"));

	/** Display/validation metadata mirrored from the BP variable to its field. */
	const TArray<FName>& PayloadMetaKeys()
	{
		static const TArray<FName> Keys = {
			TEXT("tooltip"), TEXT("ClampMin"), TEXT("ClampMax"),
			TEXT("UIMin"), TEXT("UIMax"), TEXT("Units"), TEXT("Category")
		};
		return Keys;
	}

	struct FDesiredField
	{
		FString               Name;
		FEdGraphPinType       PinType;
		FString               DefaultValue;
		FGuid                 SourceGuid;
		TMap<FName, FString>  Meta;
	};

	FString PayloadAssetName(const UBlueprint* Blueprint)
	{
		return Blueprint->GetName() + TEXT("Data");
	}

	/** UDS metadata key stamping which Blueprint OWNS the payload struct. The
	 *  CDO-linked pointer alone is NOT a safe resolution source: a derived BP's
	 *  CDO inherits the PARENT's PayloadStruct via ordinary archetype copy, and
	 *  trusting it would hijack (rename + reshape) the parent's asset. */
	const FName SeinSourceBlueprintKey(TEXT("SeinSourceBlueprint"));

	bool IsPayloadOwnedByBlueprint(const UUserDefinedStruct* UDS, const UBlueprint* Blueprint)
	{
		if (!UDS) return false;
		if (UDS->HasMetaData(SeinSourceBlueprintKey))
		{
			return UDS->GetMetaData(SeinSourceBlueprintKey)
				== Blueprint->GetPathName();
		}
		// Unstamped (pre-stamp asset): adopt only when it already follows this
		// BP's naming convention (the conventional sibling asset).
		return UDS->GetName() == PayloadAssetName(Blueprint);
	}

	UUserDefinedStruct* ResolveExistingPayloadUDS(UBlueprint* Blueprint)
	{
		if (UClass* GenClass = Blueprint->GeneratedClass)
		{
			if (const USeinDataComponent* CDO = Cast<USeinDataComponent>(
				GenClass->GetDefaultObject(/*bCreateIfNeeded*/ false)))
			{
				if (CDO->PayloadStruct
					&& IsPayloadOwnedByBlueprint(CDO->PayloadStruct, Blueprint))
				{
					return CDO->PayloadStruct;
				}
			}
		}
		const FString PkgDir  = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName());
		const FString AssetNm = PayloadAssetName(Blueprint);
		const FString ObjPath = PkgDir / (AssetNm + TEXT(".") + AssetNm);
		UUserDefinedStruct* OnDisk = LoadObject<UUserDefinedStruct>(
			nullptr, *ObjPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		return (OnDisk && IsPayloadOwnedByBlueprint(OnDisk, Blueprint))
			? OnDisk : nullptr;
	}

	UUserDefinedStruct* CreatePayloadUDS(UBlueprint* Blueprint)
	{
		const FString PkgDir  = FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName());
		const FString AssetNm = PayloadAssetName(Blueprint);

		UPackage* Package = CreatePackage(*(PkgDir / AssetNm));
		if (!Package) return nullptr;

		UUserDefinedStruct* UDS = FStructureEditorUtils::CreateUserDefinedStruct(
			Package, FName(*AssetNm), RF_Public | RF_Standalone | RF_Transactional);
		if (!UDS) return nullptr;

		// Top-level entity component: deterministic + entity-component metas,
		// so the baked entries pass the picker filter, eligibility screens,
		// and determinism validator exactly like a hand-authored UDS component.
		USeinSimComponentFactory::MarkUserDefinedStructAsEntityComponent(UDS);

		FAssetRegistryModule::AssetCreated(UDS);
		Package->MarkPackageDirty();
		return UDS;
	}

	void RenamePayloadUDS(UUserDefinedStruct* UDS, const FString& NewPackagePath, const FString& NewName)
	{
		if (!UDS) return;
		const FString CurPath = FPackageName::GetLongPackagePath(UDS->GetOutermost()->GetName());
		if (UDS->GetName() == NewName && CurPath == NewPackagePath) return;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<FAssetRenameData> Renames;
		Renames.Add(FAssetRenameData(UDS, NewPackagePath, NewName));
		AssetToolsModule.Get().RenameAssets(Renames);
	}

	/** Returns true when anything about the field actually changed. */
	bool ApplyFieldMetaAndDefault(UUserDefinedStruct* UDS, const FGuid& FieldGuid, const FDesiredField& D)
	{
		bool bChanged = false;
		const FString* StoredGuid = FStructureEditorUtils::GetMetaData(UDS, FieldGuid, SeinSourceVarGuidKey);
		if (!StoredGuid || *StoredGuid != D.SourceGuid.ToString())
		{
			FStructureEditorUtils::SetMetaData(UDS, FieldGuid, SeinSourceVarGuidKey, D.SourceGuid.ToString());
			bChanged = true;
		}

		if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, FieldGuid))
		{
			if (VD->DefaultValue != D.DefaultValue)
			{
				FStructureEditorUtils::ChangeVariableDefaultValue(UDS, FieldGuid, D.DefaultValue);
				bChanged = true;
			}
		}

		for (const FName& Key : PayloadMetaKeys())
		{
			const FString* Value = D.Meta.Find(Key);
			const FString* Existing = FStructureEditorUtils::GetMetaData(UDS, FieldGuid, Key);
			if (Value)
			{
				if (!Existing || *Existing != *Value)
				{
					FStructureEditorUtils::SetMetaData(UDS, FieldGuid, Key, *Value);
					bChanged = true;
				}
			}
			else if (Existing && !Existing->IsEmpty())
			{
				FStructureEditorUtils::SetMetaData(UDS, FieldGuid, Key, FString());
				bChanged = true;
			}
		}
		return bChanged;
	}

	/** GUID-keyed field reconciliation — same discipline as the movement
	 *  export's SyncFields (see its docstring). Returns true on any change. */
	bool SyncFields(UUserDefinedStruct* UDS, const TArray<FDesiredField>& Desired)
	{
		bool bChanged = false;

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

		for (const FDesiredField& D : Desired)
		{
			FGuid FieldGuid;
			if (FindField(D, FieldGuid))
			{
				if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, FieldGuid))
				{
					if (VD->FriendlyName != D.Name)
					{
						FStructureEditorUtils::RenameVariable(UDS, FieldGuid, D.Name);
						bChanged = true;
					}
					if (VD->ToPinType() != D.PinType)
					{
						FStructureEditorUtils::ChangeVariableType(UDS, FieldGuid, D.PinType);
						bChanged = true;
					}
				}
				bChanged |= ApplyFieldMetaAndDefault(UDS, FieldGuid, D);
				continue;
			}

			TSet<FGuid> Before;
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS)) { Before.Add(V.VarGuid); }
			if (!FStructureEditorUtils::AddVariable(UDS, D.PinType)) { continue; }
			bChanged = true;

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
		for (const FGuid& G : ToRemove)
		{
			FStructureEditorUtils::RemoveVariable(UDS, G);
			bChanged = true;
		}
		return bChanged;
	}

	/** Stamp `Struct` onto the CDO and every LOADED template/instance of the
	 *  class whose link is stale — objects copy the template value only at
	 *  creation, so pre-sync placed components would otherwise bake nothing.
	 *  Strictly no-op when already linked: an unconditional write here dirtied
	 *  every data-component Blueprint's package on mere load (the sync runs
	 *  from load-time compiles too). */
	void StampPayloadStruct(UBlueprint* Blueprint, UUserDefinedStruct* Struct)
	{
		UClass* GenClass = Blueprint->GeneratedClass;
		if (!GenClass) return;
		USeinDataComponent* CDO = Cast<USeinDataComponent>(
			GenClass->GetDefaultObject(/*bCreateIfNeeded*/ false));
		if (!CDO) return;

		const TObjectPtr<UUserDefinedStruct> Previous = CDO->PayloadStruct;
		if (Previous != Struct)
		{
			CDO->Modify();
			CDO->PayloadStruct = Struct;
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}

		for (TObjectIterator<USeinDataComponent> It; It; ++It)
		{
			USeinDataComponent* Object = *It;
			if (!Object || Object == CDO || !Object->IsA(GenClass))
			{
				continue;
			}
			// Direct instances AND templates (unit-BP SCS templates, child
			// actor templates) all need the live link; only stale links are
			// touched, and each write is transacted + dirtied so level
			// packages persist it instead of re-syncing every session.
			if (Object->PayloadStruct != Struct
				&& (Object->PayloadStruct == Previous
					|| Object->PayloadStruct == nullptr))
			{
				Object->Modify();
				Object->PayloadStruct = Struct;
				if (UPackage* Package = Object->GetPackage())
				{
					if (!Package->HasAnyPackageFlags(PKG_PlayInEditor))
					{
						Package->MarkPackageDirty();
					}
				}
			}
		}
	}
}

bool IsDataComponentBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint && Blueprint->GeneratedClass
		&& Blueprint->GeneratedClass->IsChildOf(USeinDataComponent::StaticClass());
}

UUserDefinedStruct* SyncPayloadStructForBlueprint(UBlueprint* Blueprint)
{
	if (!IsDataComponentBlueprint(Blueprint))
	{
		return nullptr;
	}

	// Every deterministic-typed variable IS component data. Non-deterministic
	// variables are excluded here and warned about by the compile gate.
	UClass* GenClass = Blueprint->GeneratedClass;
	UObject* ClassDefault = GenClass
		? GenClass->GetDefaultObject(/*bCreateIfNeeded*/ false) : nullptr;
	TArray<FDesiredField> Desired;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (!SeinDeterminism::IsPinTypeDeterministic(Var.VarType))
		{
			continue;
		}
		FDesiredField Field;
		Field.Name         = Var.VarName.ToString();
		Field.PinType      = Var.VarType;
		Field.DefaultValue = Var.DefaultValue;
		Field.SourceGuid   = Var.VarGuid;
		// FBPVariableDescription::DefaultValue is creation-time stale for
		// Class-Defaults edits (struct-typed vars in particular can ONLY be
		// defaulted there, so their string is usually empty). The live CDO
		// property is the truth — export it, same as the engine does where it
		// needs the real default.
		if (ClassDefault && GenClass)
		{
			if (const FProperty* LiveProperty =
				GenClass->FindPropertyByName(Var.VarName))
			{
				FString LiveDefault;
				LiveProperty->ExportText_Direct(LiveDefault,
					LiveProperty->ContainerPtrToValuePtr<const void>(ClassDefault),
					nullptr, nullptr, PPF_None);
				Field.DefaultValue = MoveTemp(LiveDefault);
			}
		}
		for (const FName& Key : PayloadMetaKeys())
		{
			if (Var.HasMetaData(Key)) { Field.Meta.Add(Key, Var.GetMetaData(Key)); }
		}
		Desired.Add(MoveTemp(Field));
	}

	if (Desired.Num() == 0)
	{
		StampPayloadStruct(Blueprint, nullptr);
		UE_LOG(LogSeinDataComponentSync, Warning,
			TEXT("%s has no deterministic variables; its payload link is cleared and unit bakes will DEFER managing its entries until a variable exists again."),
			*Blueprint->GetName());
		return nullptr;
	}

	UUserDefinedStruct* UDS = ResolveExistingPayloadUDS(Blueprint);
	if (UDS)
	{
		RenamePayloadUDS(UDS,
			FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName()),
			PayloadAssetName(Blueprint));
	}
	else
	{
		UDS = CreatePayloadUDS(Blueprint);
	}
	if (!UDS)
	{
		UE_LOG(LogSeinDataComponentSync, Warning,
			TEXT("Failed to get/create the payload struct for %s."), *Blueprint->GetName());
		return nullptr;
	}

	// Durable markers reapplied every pass (same migration posture as the
	// movement export), plus the owner stamp that keeps a derived Blueprint's
	// inherited PayloadStruct pointer from hijacking this asset.
	USeinSimComponentFactory::MarkUserDefinedStructAsEntityComponent(UDS);
	if (!UDS->HasMetaData(SeinSourceBlueprintKey)
		|| UDS->GetMetaData(SeinSourceBlueprintKey) != Blueprint->GetPathName())
	{
		UDS->SetMetaData(SeinSourceBlueprintKey, *Blueprint->GetPathName());
	}
	const bool bFieldsChanged = SyncFields(UDS, Desired);
	if (bFieldsChanged)
	{
		// Recompiles the UDS and migrates every serialized instance of it
		// (baked ComponentData entries included) on UE's own reinstancing
		// rails. Skipped when nothing changed so the auto-sync hook cannot
		// churn structures every compile.
		FStructureEditorUtils::OnStructureChanged(UDS, FStructureEditorUtils::Unknown);
	}
	StampPayloadStruct(Blueprint, UDS);

	UE_LOG(LogSeinDataComponentSync, Log,
		TEXT("%s -> %s (%d field(s)%s)."),
		*Blueprint->GetName(), *UDS->GetName(), Desired.Num(),
		bFieldsChanged ? TEXT(", updated") : TEXT(", unchanged"));
	return UDS;
}

}  // namespace SeinDataComponentSync
