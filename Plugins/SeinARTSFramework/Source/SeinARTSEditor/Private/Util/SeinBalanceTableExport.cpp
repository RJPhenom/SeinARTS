/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceTableExport.cpp
 */

#include "Util/SeinBalanceTableExport.h"
#include "Util/SeinBalanceColumn.h"
#include "Util/SeinDeterminismRules.h"
#include "Balance/SeinBalanceProfile.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinIdentityComponent.h"
#include "Components/SeinComponentEligibility.h"
#include "Types/FixedPoint.h"

#include "Engine/DataTable.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"  // FStructVariableDescription
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBalanceTable, Log, All);

namespace SeinBalanceTable
{
namespace
{
	/** UDS-field metadata key holding the column's stable source identity (rename-safe sync). */
	const FName SeinBalanceSourceKey(TEXT("SeinBalanceSource"));

	/** Short, collision-resistant label for a component struct: "FSeinMovementComponent" → "Movement". */
	FString DeriveComponentLabel(const UScriptStruct* Struct)
	{
		FString Name = Struct->GetName();          // reflected name has no leading 'F'
		Name.RemoveFromStart(TEXT("Sein"));
		Name.RemoveFromEnd(TEXT("Component"));
		return Name.IsEmpty() ? Struct->GetName() : Name;
	}

	/** The CDO's entity-bridge (a native default subobject on every ASeinActor). */
	USeinEntityComponent* FindBridge(const UClass* Target)
	{
		if (!Target) return nullptr;
		ASeinActor* CDO = Cast<ASeinActor>(Target->GetDefaultObject());
		return CDO ? CDO->FindComponentByClass<USeinEntityComponent>() : nullptr;
	}

	/** Two FProperties safe to CopyCompleteValue between (same class; same Struct/Enum where it matters). */
	bool PropsCompatible(const FProperty* A, const FProperty* B)
	{
		if (!A || !B || A->GetClass() != B->GetClass()) return false;
		if (const FStructProperty* SA = CastField<FStructProperty>(A))
		{
			const FStructProperty* SB = CastField<FStructProperty>(B);
			return SB && SA->Struct == SB->Struct;
		}
		if (const FEnumProperty* EA = CastField<FEnumProperty>(A))
		{
			const FEnumProperty* EB = CastField<FEnumProperty>(B);
			return EB && EA->GetEnum() == EB->GetEnum();
		}
		if (const FByteProperty* BA = CastField<FByteProperty>(A))
		{
			const FByteProperty* BB = CastField<FByteProperty>(B);
			return BB && BA->Enum == BB->Enum;
		}
		return true;  // numeric / bool / name / text — matching FProperty class is enough
	}

	/** Shared read path for Component + Identity columns: locate the ComponentData entry of
	 *  `CompStruct` on the target CDO and copy `SrcProp` → `DestProp`. False if absent. */
	bool ReadComponentFieldInto(const UClass* Target, const UScriptStruct* CompStruct,
		const FProperty* SrcProp, bool bFixedToFloat, const FProperty* DestProp, void* DestPtr)
	{
		if (!CompStruct || !SrcProp || !DestProp || !DestPtr) return false;
		const USeinEntityComponent* Bridge = FindBridge(Target);
		if (!Bridge) return false;

		for (const FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (Entry.GetScriptStruct() != CompStruct) continue;
			const void* SrcPtr = SrcProp->ContainerPtrToValuePtr<void>(Entry.GetMemory());

			if (bFixedToFloat)
			{
				// Source FFixedPoint → float/double display column. Editor-only ToFloat (see header).
				const float AsFloat = static_cast<const FFixedPoint*>(SrcPtr)->ToFloat();
				if (const FFloatProperty* FloatDest = CastField<FFloatProperty>(DestProp))
				{
					FloatDest->SetPropertyValue(DestPtr, AsFloat);
					return true;
				}
				if (const FDoubleProperty* DoubleDest = CastField<FDoubleProperty>(DestProp))
				{
					DoubleDest->SetPropertyValue(DestPtr, static_cast<double>(AsFloat));
					return true;
				}
				return false;
			}

			if (!PropsCompatible(SrcProp, DestProp)) return false;
			DestProp->CopyCompleteValue(DestPtr, SrcPtr);
			return true;
		}
		return false;  // component not authored on this entity → leave default (sparse cell)
	}

	// ---- Providers ---------------------------------------------------------------------------

	/** Top-level deterministic fields of the profile's tracked components (or, if none are
	 *  listed, every eligible component found across the matched entities). */
	class FComponentColumnProvider : public ISeinBalanceColumnProvider
	{
	public:
		virtual void DescribeColumns(const TArray<UClass*>& Targets, const USeinBalanceProfile& Profile,
			TArray<FSeinBalanceColumn>& OutColumns) const override
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			const UScriptStruct* IdentityStruct = FSeinIdentityComponent::StaticStruct();

			// Which component structs to expand into columns.
			TArray<UScriptStruct*> Structs;
			if (Profile.TrackedComponents.Num() > 0)
			{
				for (const TObjectPtr<UScriptStruct>& S : Profile.TrackedComponents)
				{
					if (S && S != IdentityStruct) Structs.AddUnique(S);  // Identity handled by its own provider
				}
			}
			else
			{
				TSet<UScriptStruct*> Found;
				for (const UClass* T : Targets)
				{
					const USeinEntityComponent* Bridge = FindBridge(T);
					if (!Bridge) continue;
					for (const FInstancedStruct& E : Bridge->ComponentData)
					{
						UScriptStruct* SS = const_cast<UScriptStruct*>(E.GetScriptStruct());
						if (!SS || SS == IdentityStruct) continue;
						if (SeinComponentEligibility::IsEntityComponentStruct(SS)) Found.Add(SS);
					}
				}
				Structs = Found.Array();
			}
			Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName() < B.GetName(); });

			for (UScriptStruct* SS : Structs)
			{
				const FString Label = DeriveComponentLabel(SS);
				for (TFieldIterator<FProperty> It(SS); It; ++It)
				{
					FProperty* Prop = *It;
					if (Prop->GetOwnerStruct() != SS) continue;  // own fields only (skip FSeinComponent base)

					// Designer-authored fields only (EditAnywhere/EditDefaultsOnly). Excludes the
					// component's runtime state — BlueprintReadWrite-only fields like Velocity,
					// TargetLocation, SmoothedPitch, bInitialGroundSnapDone — which are sim state,
					// not tunables. CPF_Edit is the authoring signal; CPF_EditConst drops VisibleAnywhere.
					if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_EditConst)) continue;

					FEdGraphPinType PinType;
					if (!Schema->ConvertPropertyToPinType(Prop, PinType)) continue;
					if (!SeinDeterminism::IsPinTypeDeterministic(PinType)) continue;  // tunable fields only

					FSeinBalanceColumn Col;
					Col.DisplayName    = Label + TEXT("_") + SS->GetAuthoredNameForField(Prop);
					Col.SourceKey      = FString(TEXT("C|")) + SS->GetPathName() + TEXT("|") + Prop->GetName();
					Col.Kind           = ESeinBalanceColumnKind::Component;
					Col.ComponentStruct = SS;
					Col.SourceProp     = Prop;

					// FFixedPoint renders as `{ "Value": <int64> }` in the DataTable grid — unreadable.
					// Surface it as a plain float column instead (editor display only; converted fixed↔float
					// at gather/push, the conversion FSeinFixedPointDetails already uses). Other deterministic
					// types (int, bool, enum) keep their native pin and read as-is.
					const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
					if (StructProp && StructProp->Struct == FFixedPoint::StaticStruct())
					{
						Col.PinType.PinCategory    = UEdGraphSchema_K2::PC_Real;
						Col.PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
						Col.bConvertFixedToFloat   = true;
					}
					else
					{
						Col.PinType = PinType;
					}
					OutColumns.Add(MoveTemp(Col));
				}
			}
		}

		virtual bool ReadInto(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* DestProp, void* DestPtr) const override
		{
			return ReadComponentFieldInto(Target, Column.ComponentStruct.Get(), Column.SourceProp, Column.bConvertFixedToFloat, DestProp, DestPtr);
		}
	};

	/** DisplayName + IdentityTag from FSeinIdentityComponent — read-only row labels. Emitted only
	 *  in track-all mode or when the profile explicitly lists FSeinIdentityComponent. */
	class FIdentityColumnProvider : public ISeinBalanceColumnProvider
	{
	public:
		virtual void DescribeColumns(const TArray<UClass*>& /*Targets*/, const USeinBalanceProfile& Profile,
			TArray<FSeinBalanceColumn>& OutColumns) const override
		{
			UScriptStruct* IdStruct = FSeinIdentityComponent::StaticStruct();
			const bool bWanted = Profile.TrackedComponents.Num() == 0
				|| Profile.TrackedComponents.Contains(IdStruct);
			if (!bWanted) return;

			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			auto AddIdColumn = [&](const TCHAR* FieldName, const TCHAR* Display)
			{
				FProperty* Prop = IdStruct->FindPropertyByName(FieldName);
				if (!Prop) return;
				FEdGraphPinType PinType;
				if (!Schema->ConvertPropertyToPinType(Prop, PinType)) return;

				FSeinBalanceColumn Col;
				Col.DisplayName    = Display;
				Col.PinType        = PinType;
				Col.SourceKey      = FString(TEXT("I|")) + FieldName;
				Col.Kind           = ESeinBalanceColumnKind::Identity;
				Col.ComponentStruct = IdStruct;
				Col.SourceProp     = Prop;
				OutColumns.Add(MoveTemp(Col));
			};
			AddIdColumn(TEXT("DisplayName"), TEXT("Identity_DisplayName"));
			AddIdColumn(TEXT("IdentityTag"), TEXT("Identity_IdentityTag"));
		}

		virtual bool ReadInto(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* DestProp, void* DestPtr) const override
		{
			return ReadComponentFieldInto(Target, Column.ComponentStruct.Get(), Column.SourceProp, Column.bConvertFixedToFloat, DestProp, DestPtr);
		}
	};

	// ---- UDS schema sync ---------------------------------------------------------------------

	/** Reconcile the row UDS's fields to `Desired`, matching by the stamped source key. Adds new
	 *  fields, retypes changed ones, renames on a source rename, and drops strays + the default
	 *  member. The caller empties the owning table first, so there are no rows to migrate. */
	void SyncRowStructFields(UUserDefinedStruct* UDS, const TArray<FSeinBalanceColumn>& Desired)
	{
		if (!UDS) return;

		auto FindFieldByKey = [UDS](const FString& Key, FGuid& OutGuid) -> bool
		{
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				const FString* Stored = FStructureEditorUtils::GetMetaData(UDS, V.VarGuid, SeinBalanceSourceKey);
				if (Stored && *Stored == Key) { OutGuid = V.VarGuid; return true; }
			}
			return false;
		};

		// Pass 1 — ensure each desired column exists; reconcile name + type.
		for (const FSeinBalanceColumn& D : Desired)
		{
			FGuid FieldGuid;
			if (FindFieldByKey(D.SourceKey, FieldGuid))
			{
				if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, FieldGuid))
				{
					if (VD->FriendlyName != D.DisplayName) { FStructureEditorUtils::RenameVariable(UDS, FieldGuid, D.DisplayName); }
					if (VD->ToPinType()  != D.PinType)     { FStructureEditorUtils::ChangeVariableType(UDS, FieldGuid, D.PinType); }
				}
				continue;
			}

			// New column: AddVariable auto-names; snapshot GUIDs to find it, then rename + stamp.
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
				FStructureEditorUtils::RenameVariable(UDS, NewGuid, D.DisplayName);
				FStructureEditorUtils::SetMetaData(UDS, NewGuid, SeinBalanceSourceKey, D.SourceKey);
			}
		}

		// Pass 2 — drop fields whose source key is no longer desired (and the default un-stamped
		// member from CreateUserDefinedStruct). Desired is non-empty + added above, so never empty.
		TSet<FString> DesiredKeys;
		for (const FSeinBalanceColumn& D : Desired) { DesiredKeys.Add(D.SourceKey); }

		TArray<FGuid> ToRemove;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
		{
			const FString* Stored = FStructureEditorUtils::GetMetaData(UDS, V.VarGuid, SeinBalanceSourceKey);
			const bool bKeep = Stored && DesiredKeys.Contains(*Stored);
			if (!bKeep) { ToRemove.Add(V.VarGuid); }
		}
		for (const FGuid& G : ToRemove) { FStructureEditorUtils::RemoveVariable(UDS, G); }
	}

	/** A row-UDS field, located by its authored (friendly) name. */
	FProperty* FindFieldByAuthoredName(const UUserDefinedStruct* UDS, const FString& AuthoredName)
	{
		for (TFieldIterator<FProperty> It(UDS); It; ++It)
		{
			if (UDS->GetAuthoredNameForField(*It) == AuthoredName) { return *It; }
		}
		return nullptr;
	}

	// ---- Asset get-or-create -----------------------------------------------------------------

	UUserDefinedStruct* GetOrCreateRowUDS(const FString& Dir, const FString& Name)
	{
		const FString PkgName = Dir / Name;
		if (UUserDefinedStruct* Existing = LoadObject<UUserDefinedStruct>(nullptr, *(PkgName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UPackage* Pkg = CreatePackage(*PkgName);
		if (!Pkg) return nullptr;
		UUserDefinedStruct* UDS = FStructureEditorUtils::CreateUserDefinedStruct(
			Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		if (UDS)
		{
			FAssetRegistryModule::AssetCreated(UDS);
			Pkg->MarkPackageDirty();
		}
		return UDS;
	}

	UDataTable* GetOrCreateDataTable(const FString& Dir, const FString& Name, UUserDefinedStruct* RowUDS)
	{
		const FString PkgName = Dir / Name;
		if (UDataTable* Existing = LoadObject<UDataTable>(nullptr, *(PkgName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UPackage* Pkg = CreatePackage(*PkgName);
		if (!Pkg) return nullptr;
		UDataTable* Table = NewObject<UDataTable>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		Table->RowStruct = RowUDS;
		FAssetRegistryModule::AssetCreated(Table);
		Pkg->MarkPackageDirty();
		return Table;
	}

	/** Stable, readable row key for a target class: the Blueprint asset name (or native class name). */
	FName RowNameForClass(const UClass* T)
	{
		if (T->ClassGeneratedBy) { return FName(*T->ClassGeneratedBy->GetName()); }
		FString N = T->GetName();
		N.RemoveFromEnd(TEXT("_C"));
		return FName(*N);
	}
}  // anonymous namespace

UDataTable* GatherToTable(USeinBalanceProfile* Profile)
{
	if (!Profile) return nullptr;

	// 1. Targets.
	TArray<UClass*> Targets;
	Profile->ResolveTargetClasses(Targets);
	if (Targets.Num() == 0)
	{
		UE_LOG(LogSeinBalanceTable, Warning, TEXT("Gather: profile '%s' matched no classes."), *Profile->GetName());
		return nullptr;
	}

	// 2. Providers → desired columns (deduped by source key).
	FComponentColumnProvider ComponentProvider;
	FIdentityColumnProvider  IdentityProvider;

	TArray<FSeinBalanceColumn> Columns;
	ComponentProvider.DescribeColumns(Targets, *Profile, Columns);
	IdentityProvider.DescribeColumns(Targets, *Profile, Columns);
	{
		TSet<FString> SeenKeys;
		Columns.RemoveAll([&SeenKeys](const FSeinBalanceColumn& C)
		{
			bool bAlready = false; SeenKeys.Add(C.SourceKey, &bAlready); return bAlready;
		});
	}
	if (Columns.Num() == 0)
	{
		UE_LOG(LogSeinBalanceTable, Warning, TEXT("Gather: profile '%s' produced no columns."), *Profile->GetName());
		return nullptr;
	}

	// 3. Resolve paths + any EXISTING assets up front — BEFORE mutating anything. Loading a table
	//    AFTER its row struct has changed deserializes its saved rows against the new layout, which
	//    reads a garbage container count and crashes. So we resolve (load) first, mutate later.
	const FString OutDir = Profile->OutputDir.Path.IsEmpty()
		? FPackageName::GetLongPackagePath(Profile->GetOutermost()->GetName())
		: Profile->OutputDir.Path;
	const FString BaseName  = Profile->GetName();
	const FString TableName = FString(TEXT("DT_")) + BaseName;
	const FString RowName   = BaseName + TEXT("_Row");

	UDataTable* Table = Profile->GeneratedTable.LoadSynchronous();
	if (!Table)
	{
		Table = LoadObject<UDataTable>(nullptr, *(OutDir / TableName + TEXT(".") + TableName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	UUserDefinedStruct* RowUDS = Table ? Cast<UUserDefinedStruct>(Table->RowStruct) : nullptr;
	if (!RowUDS)
	{
		RowUDS = LoadObject<UUserDefinedStruct>(nullptr, *(OutDir / RowName + TEXT(".") + RowName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// 4. Destructive regenerate: if a table already exists, warn first — Gather REBUILDS it from the
	//    source Blueprints and discards any manual in-table edits (write-back is the path back to BPs).
	if (Table)
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("SeinBalanceTable", "RegenWarn",
				"'{0}' already exists.\n\nRe-gathering REBUILDS it from the source Blueprints and DISCARDS "
				"any edits made directly in the table. Continue?"),
			FText::FromString(TableName)));
		if (Choice != EAppReturnType::Yes) { return nullptr; }
	}

	// 5. Close any open editors for the assets we're about to rewrite — mutating an open struct/table
	//    thrashes the live editor and can deadlock the table change-scope. The table reopens at the end.
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (Table)  { AES->CloseAllEditorsForAsset(Table); }
			if (RowUDS) { AES->CloseAllEditorsForAsset(RowUDS); }
		}
	}

	// 6. Empty the table BEFORE rewriting the row struct — with no rows there is nothing to migrate,
	//    so the schema rewrite can't hit a stale-layout deserialize. This is what makes regen safe.
	if (Table) { Table->EmptyTable(); }

	// 7. Get-or-create the row UDS, then rewrite its fields to exactly the desired columns.
	if (!RowUDS) { RowUDS = GetOrCreateRowUDS(OutDir, RowName); }
	if (!RowUDS)
	{
		UE_LOG(LogSeinBalanceTable, Warning, TEXT("Gather: failed to create row struct for '%s'."), *Profile->GetName());
		return nullptr;
	}
	SyncRowStructFields(RowUDS, Columns);
	FStructureEditorUtils::OnStructureChanged(RowUDS, FStructureEditorUtils::Unknown);

	if (!Table) { Table = GetOrCreateDataTable(OutDir, TableName, RowUDS); }
	if (!Table)
	{
		UE_LOG(LogSeinBalanceTable, Warning, TEXT("Gather: failed to create DataTable for '%s'."), *Profile->GetName());
		return nullptr;
	}
	Table->RowStruct = RowUDS;

	// 8. Populate every row fresh from the source ComponentData (the table is empty after step 6).
	auto FillCell = [&](const FSeinBalanceColumn& Col, const UClass* Target, uint8* RowData)
	{
		FProperty* DestProp = FindFieldByAuthoredName(RowUDS, Col.DisplayName);
		if (!DestProp) return;
		void* DestPtr = DestProp->ContainerPtrToValuePtr<void>(RowData);
		const ISeinBalanceColumnProvider& P = (Col.Kind == ESeinBalanceColumnKind::Identity)
			? static_cast<const ISeinBalanceColumnProvider&>(IdentityProvider)
			: static_cast<const ISeinBalanceColumnProvider&>(ComponentProvider);
		P.ReadInto(Target, Col, DestProp, DestPtr);
	};

	int32 NumRows = 0;
	for (const UClass* T : Targets)
	{
		uint8* Buf = static_cast<uint8*>(FMemory::Malloc(RowUDS->GetStructureSize()));
		RowUDS->InitializeStruct(Buf);
		for (const FSeinBalanceColumn& Col : Columns) { FillCell(Col, T, Buf); }
		Table->AddRow(RowNameForClass(T), Buf, RowUDS);
		RowUDS->DestroyStruct(Buf);
		FMemory::Free(Buf);
		++NumRows;
	}

	Table->HandleDataTableChanged();

	// 6. Link to the profile, dirty everything, open the table.
	Profile->GeneratedTable = Table;
	Profile->MarkPackageDirty();
	Table->MarkPackageDirty();
	RowUDS->MarkPackageDirty();

	UE_LOG(LogSeinBalanceTable, Log, TEXT("Gather: '%s' → '%s' (%d row(s), %d column(s))."),
		*Profile->GetName(), *Table->GetName(), NumRows, Columns.Num());

	if (GEditor)
	{
		if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AES->OpenEditorForAsset(Table);
		}
	}
	return Table;
}

}  // namespace SeinBalanceTable
