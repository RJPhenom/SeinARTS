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
#include "Abilities/SeinAbility.h"
#include "Data/SeinResourceTypes.h"
#include "Settings/PluginSettings.h"

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
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

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

	/** Read a row cell that is a float/double display column as a double. */
	double ReadFloatCell(const FProperty* CellProp, const void* CellPtr)
	{
		if (const FFloatProperty* FP = CastField<FFloatProperty>(CellProp)) return FP->GetPropertyValue(CellPtr);
		if (const FDoubleProperty* DP = CastField<FDoubleProperty>(CellProp)) return DP->GetPropertyValue(CellPtr);
		return 0.0;
	}

	/** Navigate a NestedComponent column to its inner sub-data memory on the target CDO: find the
	 *  component entry, read its FInstancedStruct field, and confirm the inner type matches the
	 *  column's. Null if the component/field/inner-type doesn't match (a sparse cell). */
	const void* FindNestedInner(const UClass* Target, const FSeinBalanceColumn& Col)
	{
		const USeinEntityComponent* Bridge = FindBridge(Target);
		const FStructProperty* ContainerSP = CastField<FStructProperty>(Col.NestedContainerProp);
		if (!Bridge || !Col.ComponentStruct.Get() || !ContainerSP || !Col.InnerStruct.Get()) return nullptr;
		for (const FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (Entry.GetScriptStruct() != Col.ComponentStruct.Get()) continue;
			const FInstancedStruct* Nested = ContainerSP->ContainerPtrToValuePtr<FInstancedStruct>(Entry.GetMemory());
			if (!Nested || Nested->GetScriptStruct() != Col.InnerStruct.Get()) return nullptr;
			return Nested->GetMemory();
		}
		return nullptr;
	}

	/** Mutable counterpart of FindNestedInner — for Push write-back into the inner sub-data. */
	void* FindNestedInnerMutable(const UClass* Target, const FSeinBalanceColumn& Col)
	{
		USeinEntityComponent* Bridge = FindBridge(Target);
		const FStructProperty* ContainerSP = CastField<FStructProperty>(Col.NestedContainerProp);
		if (!Bridge || !Col.ComponentStruct.Get() || !ContainerSP || !Col.InnerStruct.Get()) return nullptr;
		for (FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (Entry.GetScriptStruct() != Col.ComponentStruct.Get()) continue;
			FInstancedStruct* Nested = ContainerSP->ContainerPtrToValuePtr<FInstancedStruct>(Entry.GetMutableMemory());
			if (!Nested || Nested->GetScriptStruct() != Col.InnerStruct.Get()) return nullptr;
			return Nested->GetMutableMemory();
		}
		return nullptr;
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

		virtual ESeinBalanceWriteResult WriteFrom(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* CellProp, const void* CellPtr) const override
		{
			USeinEntityComponent* Bridge = FindBridge(Target);
			UScriptStruct* CompStruct = Column.ComponentStruct.Get();
			const FProperty* DestField = Column.SourceProp;
			if (!Bridge || !CompStruct || !DestField || !CellProp || !CellPtr) return ESeinBalanceWriteResult::Skipped;

			for (FInstancedStruct& Entry : Bridge->ComponentData)
			{
				if (Entry.GetScriptStruct() != CompStruct) continue;
				void* DestPtr = DestField->ContainerPtrToValuePtr<void>(Entry.GetMutableMemory());

				if (Column.bConvertFixedToFloat)
				{
					FFixedPoint& Fixed = *static_cast<FFixedPoint*>(DestPtr);
					const float NewVal = static_cast<float>(ReadFloatCell(CellProp, CellPtr));
					if (NewVal == Fixed.ToFloat()) return ESeinBalanceWriteResult::Unchanged;  // don't perturb
					Fixed = FFixedPoint::FromFloat(NewVal);
					return ESeinBalanceWriteResult::Wrote;
				}

				if (!PropsCompatible(CellProp, DestField)) return ESeinBalanceWriteResult::Skipped;
				if (DestField->Identical(DestPtr, CellPtr)) return ESeinBalanceWriteResult::Unchanged;
				DestField->CopyCompleteValue(DestPtr, CellPtr);
				return ESeinBalanceWriteResult::Wrote;
			}
			return ESeinBalanceWriteResult::Skipped;  // component not authored on this unit
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

		virtual ESeinBalanceWriteResult WriteFrom(const UClass* /*Target*/, const FSeinBalanceColumn& /*Column*/,
			const FProperty* /*CellProp*/, const void* /*CellPtr*/) const override
		{
			return ESeinBalanceWriteResult::Skipped;  // Identity columns are display-only — never pushed
		}
	};

	/** Per-class sub-data fields nested inside a tracked component's FInstancedStruct field (e.g.
	 *  FSeinMovementComponent::MovementClassData → FSeinWheeledMovementData's Wheelbase). Discovers the
	 *  inner sub-data types actually authored across the matched entities purely by reflection (no
	 *  dependency on the Movement+ extension where those structs live), and emits a column per inner
	 *  deterministic field. A unit whose sub-data is a different inner type (or empty) gets a sparse
	 *  cell on read and is skipped on write (the inner type is never converted). */
	class FNestedComponentColumnProvider : public ISeinBalanceColumnProvider
	{
	public:
		virtual void DescribeColumns(const TArray<UClass*>& Targets, const USeinBalanceProfile& Profile,
			TArray<FSeinBalanceColumn>& OutColumns) const override
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			const UScriptStruct* IdentityStruct = FSeinIdentityComponent::StaticStruct();

			TArray<UScriptStruct*> Structs;
			if (Profile.TrackedComponents.Num() > 0)
			{
				for (const TObjectPtr<UScriptStruct>& S : Profile.TrackedComponents)
				{
					if (S && S != IdentityStruct) Structs.AddUnique(S);
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
						if (SS && SS != IdentityStruct && SeinComponentEligibility::IsEntityComponentStruct(SS)) Found.Add(SS);
					}
				}
				Structs = Found.Array();
			}
			Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName() < B.GetName(); });

			for (UScriptStruct* SS : Structs)
			{
				for (TFieldIterator<FProperty> It(SS); It; ++It)
				{
					FProperty* Prop = *It;
					if (Prop->GetOwnerStruct() != SS) continue;
					if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_EditConst)) continue;
					const FStructProperty* ContainerSP = CastField<FStructProperty>(Prop);
					if (!ContainerSP || ContainerSP->Struct != FInstancedStruct::StaticStruct()) continue;

					// Which inner sub-data types are actually authored across the matched entities?
					TSet<UScriptStruct*> InnerTypes;
					for (const UClass* T : Targets)
					{
						const USeinEntityComponent* Bridge = FindBridge(T);
						if (!Bridge) continue;
						for (const FInstancedStruct& E : Bridge->ComponentData)
						{
							if (E.GetScriptStruct() != SS) continue;
							const FInstancedStruct* Nested = ContainerSP->ContainerPtrToValuePtr<FInstancedStruct>(E.GetMemory());
							if (Nested) { if (UScriptStruct* Inner = const_cast<UScriptStruct*>(Nested->GetScriptStruct())) InnerTypes.Add(Inner); }
						}
					}
					TArray<UScriptStruct*> SortedInner = InnerTypes.Array();
					SortedInner.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName() < B.GetName(); });

					for (UScriptStruct* Inner : SortedInner)
					{
						const FString InnerLabel = DeriveComponentLabel(Inner);
						for (TFieldIterator<FProperty> InnerIt(Inner); InnerIt; ++InnerIt)
						{
							FProperty* InnerProp = *InnerIt;
							if (InnerProp->GetOwnerStruct() != Inner) continue;
							if (!InnerProp->HasAnyPropertyFlags(CPF_Edit) || InnerProp->HasAnyPropertyFlags(CPF_EditConst)) continue;

							FEdGraphPinType PinType;
							if (!Schema->ConvertPropertyToPinType(InnerProp, PinType)) continue;
							if (!SeinDeterminism::IsPinTypeDeterministic(PinType)) continue;

							FSeinBalanceColumn Col;
							Col.DisplayName        = InnerLabel + TEXT("_") + Inner->GetAuthoredNameForField(InnerProp);
							Col.SourceKey          = FString(TEXT("N|")) + SS->GetPathName() + TEXT("|") + Prop->GetName()
												   + TEXT("|") + Inner->GetPathName() + TEXT("|") + InnerProp->GetName();
							Col.Kind               = ESeinBalanceColumnKind::NestedComponent;
							Col.ComponentStruct    = SS;
							Col.NestedContainerProp = Prop;
							Col.InnerStruct        = Inner;
							Col.SourceProp         = InnerProp;

							const FStructProperty* InnerStructProp = CastField<FStructProperty>(InnerProp);
							if (InnerStructProp && InnerStructProp->Struct == FFixedPoint::StaticStruct())
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
			}
		}

		virtual bool ReadInto(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* DestProp, void* DestPtr) const override
		{
			const void* InnerMem = FindNestedInner(Target, Column);
			if (!InnerMem || !Column.SourceProp) return false;
			const void* SrcPtr = Column.SourceProp->ContainerPtrToValuePtr<void>(InnerMem);

			if (Column.bConvertFixedToFloat)
			{
				const float AsFloat = static_cast<const FFixedPoint*>(SrcPtr)->ToFloat();
				if (const FFloatProperty* FD = CastField<FFloatProperty>(DestProp)) { FD->SetPropertyValue(DestPtr, AsFloat); return true; }
				if (const FDoubleProperty* DD = CastField<FDoubleProperty>(DestProp)) { DD->SetPropertyValue(DestPtr, static_cast<double>(AsFloat)); return true; }
				return false;
			}
			if (!PropsCompatible(Column.SourceProp, DestProp)) return false;
			DestProp->CopyCompleteValue(DestPtr, SrcPtr);
			return true;
		}

		virtual ESeinBalanceWriteResult WriteFrom(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* CellProp, const void* CellPtr) const override
		{
			void* InnerMem = FindNestedInnerMutable(Target, Column);
			if (!InnerMem || !Column.SourceProp || !CellProp || !CellPtr) return ESeinBalanceWriteResult::Skipped;
			void* DestPtr = Column.SourceProp->ContainerPtrToValuePtr<void>(InnerMem);

			if (Column.bConvertFixedToFloat)
			{
				FFixedPoint& Fixed = *static_cast<FFixedPoint*>(DestPtr);
				const float NewVal = static_cast<float>(ReadFloatCell(CellProp, CellPtr));
				if (NewVal == Fixed.ToFloat()) return ESeinBalanceWriteResult::Unchanged;
				Fixed = FFixedPoint::FromFloat(NewVal);
				return ESeinBalanceWriteResult::Wrote;
			}
			if (!PropsCompatible(CellProp, Column.SourceProp)) return ESeinBalanceWriteResult::Skipped;
			if (Column.SourceProp->Identical(DestPtr, CellPtr)) return ESeinBalanceWriteResult::Unchanged;
			Column.SourceProp->CopyCompleteValue(DestPtr, CellPtr);
			return ESeinBalanceWriteResult::Wrote;
		}
	};

	/** Deterministic authored fields directly on a targeted USeinAbility class CDO (Cooldown, MaxRange,
	 *  AreaRadius, bools, enums, ...). FFixedPoint → float columns; ResourceCost is excluded here (the
	 *  cost provider flattens it). Read/write the ability CDO directly (Push marks the ability BP dirty). */
	class FAbilityFieldColumnProvider : public ISeinBalanceColumnProvider
	{
	public:
		virtual void DescribeColumns(const TArray<UClass*>& Targets, const USeinBalanceProfile& /*Profile*/,
			TArray<FSeinBalanceColumn>& OutColumns) const override
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			for (const UClass* T : Targets)
			{
				if (!T) continue;
				for (TFieldIterator<FProperty> It(T); It; ++It)  // include inherited (Cooldown etc. on USeinAbility)
				{
					FProperty* Prop = *It;
					if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_EditConst)) continue;
					if (Prop->GetFName() == TEXT("ResourceCost")) continue;  // handled by the cost provider

					// Only FFixedPoint structs become columns; other structs (FSeinResourceCost, tag
					// containers, targeter spec) would render as JSON — skip them.
					if (const FStructProperty* SkipSP = CastField<FStructProperty>(Prop))
					{
						if (SkipSP->Struct != FFixedPoint::StaticStruct()) continue;
					}

					FEdGraphPinType PinType;
					if (!Schema->ConvertPropertyToPinType(Prop, PinType)) continue;
					if (!SeinDeterminism::IsPinTypeDeterministic(PinType)) continue;

					FSeinBalanceColumn Col;
					Col.DisplayName = T->GetAuthoredNameForField(Prop);
					Col.SourceKey   = FString(TEXT("A|")) + Prop->GetName();
					Col.Kind        = ESeinBalanceColumnKind::AbilityField;
					Col.SourceProp  = Prop;

					const FStructProperty* SP = CastField<FStructProperty>(Prop);
					if (SP && SP->Struct == FFixedPoint::StaticStruct())
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
			const UObject* CDO = Target ? Target->GetDefaultObject() : nullptr;
			if (!CDO || !Column.SourceProp) return false;
			if (!Target->IsChildOf(Column.SourceProp->GetOwnerClass())) return false;  // field not on this ability
			const void* SrcPtr = Column.SourceProp->ContainerPtrToValuePtr<void>(CDO);

			if (Column.bConvertFixedToFloat)
			{
				const float AsFloat = static_cast<const FFixedPoint*>(SrcPtr)->ToFloat();
				if (const FFloatProperty* FD = CastField<FFloatProperty>(DestProp)) { FD->SetPropertyValue(DestPtr, AsFloat); return true; }
				if (const FDoubleProperty* DD = CastField<FDoubleProperty>(DestProp)) { DD->SetPropertyValue(DestPtr, static_cast<double>(AsFloat)); return true; }
				return false;
			}
			if (!PropsCompatible(Column.SourceProp, DestProp)) return false;
			DestProp->CopyCompleteValue(DestPtr, SrcPtr);
			return true;
		}

		virtual ESeinBalanceWriteResult WriteFrom(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* CellProp, const void* CellPtr) const override
		{
			UObject* CDO = Target ? Target->GetDefaultObject() : nullptr;
			if (!CDO || !Column.SourceProp || !CellProp || !CellPtr) return ESeinBalanceWriteResult::Skipped;
			if (!Target->IsChildOf(Column.SourceProp->GetOwnerClass())) return ESeinBalanceWriteResult::Skipped;
			void* DestPtr = Column.SourceProp->ContainerPtrToValuePtr<void>(CDO);

			if (Column.bConvertFixedToFloat)
			{
				FFixedPoint& Fixed = *static_cast<FFixedPoint*>(DestPtr);
				const float NewVal = static_cast<float>(ReadFloatCell(CellProp, CellPtr));
				if (NewVal == Fixed.ToFloat()) return ESeinBalanceWriteResult::Unchanged;
				Fixed = FFixedPoint::FromFloat(NewVal);
				return ESeinBalanceWriteResult::Wrote;
			}
			if (!PropsCompatible(CellProp, Column.SourceProp)) return ESeinBalanceWriteResult::Skipped;
			if (Column.SourceProp->Identical(DestPtr, CellPtr)) return ESeinBalanceWriteResult::Unchanged;
			Column.SourceProp->CopyCompleteValue(DestPtr, CellPtr);
			return ESeinBalanceWriteResult::Wrote;
		}
	};

	/** One column per resource in the settings ResourceCatalog, reading/writing that resource's amount
	 *  in a targeted ability's `ResourceCost.Amounts` map (covers both activation cost and production/
	 *  build cost — both are ability ResourceCosts). Cost is FFixedPoint, shown as a float column. */
	class FAbilityCostColumnProvider : public ISeinBalanceColumnProvider
	{
	public:
		virtual void DescribeColumns(const TArray<UClass*>& /*Targets*/, const USeinBalanceProfile& /*Profile*/,
			TArray<FSeinBalanceColumn>& OutColumns) const override
		{
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			if (!Settings) return;
			for (const FSeinResourceDefinition& Res : Settings->ResourceCatalog)
			{
				if (!Res.ResourceTag.IsValid()) continue;
				FString Leaf;
				if (!Res.ResourceTag.ToString().Split(TEXT("."), nullptr, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					Leaf = Res.ResourceTag.ToString();
				}

				FSeinBalanceColumn Col;
				Col.DisplayName = FString(TEXT("Cost_")) + Leaf;
				Col.SourceKey   = FString(TEXT("AC|")) + Res.ResourceTag.ToString();
				Col.Kind        = ESeinBalanceColumnKind::AbilityCost;
				Col.ResourceTag = Res.ResourceTag;
				Col.PinType.PinCategory    = UEdGraphSchema_K2::PC_Real;  // float column (cost shown as decimal)
				Col.PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
				OutColumns.Add(MoveTemp(Col));
			}
		}

		virtual bool ReadInto(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* DestProp, void* DestPtr) const override
		{
			const USeinAbility* Ability = Target ? Cast<USeinAbility>(Target->GetDefaultObject()) : nullptr;
			if (!Ability) return false;
			const float AsFloat = Ability->ResourceCost.Amounts.FindRef(Column.ResourceTag).ToFloat();
			if (const FFloatProperty* FD = CastField<FFloatProperty>(DestProp)) { FD->SetPropertyValue(DestPtr, AsFloat); return true; }
			if (const FDoubleProperty* DD = CastField<FDoubleProperty>(DestProp)) { DD->SetPropertyValue(DestPtr, static_cast<double>(AsFloat)); return true; }
			return false;
		}

		virtual ESeinBalanceWriteResult WriteFrom(const UClass* Target, const FSeinBalanceColumn& Column,
			const FProperty* CellProp, const void* CellPtr) const override
		{
			USeinAbility* Ability = Target ? Cast<USeinAbility>(Target->GetDefaultObject()) : nullptr;
			if (!Ability || !CellProp || !CellPtr) return ESeinBalanceWriteResult::Skipped;
			const float NewVal = static_cast<float>(ReadFloatCell(CellProp, CellPtr));
			const float CurVal = Ability->ResourceCost.Amounts.FindRef(Column.ResourceTag).ToFloat();
			if (NewVal == CurVal) return ESeinBalanceWriteResult::Unchanged;
			if (NewVal == 0.0f) { Ability->ResourceCost.Amounts.Remove(Column.ResourceTag); }
			else                { Ability->ResourceCost.Amounts.FindOrAdd(Column.ResourceTag) = FFixedPoint::FromFloat(NewVal); }
			return ESeinBalanceWriteResult::Wrote;
		}
	};

	/** Route a column to its provider by kind. */
	const ISeinBalanceColumnProvider& ProviderForKind(
		ESeinBalanceColumnKind Kind,
		const FComponentColumnProvider& Comp, const FIdentityColumnProvider& Ident,
		const FNestedComponentColumnProvider& Nested, const FAbilityFieldColumnProvider& AbilField,
		const FAbilityCostColumnProvider& AbilCost)
	{
		switch (Kind)
		{
		case ESeinBalanceColumnKind::Identity:        return Ident;
		case ESeinBalanceColumnKind::NestedComponent: return Nested;
		case ESeinBalanceColumnKind::AbilityField:    return AbilField;
		case ESeinBalanceColumnKind::AbilityCost:     return AbilCost;
		default:                                       return Comp;
		}
	}

	// ---- UDS schema sync ---------------------------------------------------------------------

	/** Reconcile the row UDS's fields to `Desired`. Keep any existing field that already matches a
	 *  desired column EXACTLY (same source key + friendly name + pin type); for anything missing or
	 *  stale (wrong type/name) add it FRESH and remove the old one. We never `ChangeVariableType` in
	 *  place: an in-place retype (e.g. FFixedPoint→float) does NOT reliably reflect into the same-pass
	 *  populate — the field still reads as the old type, the float conversion no-ops, and cells come
	 *  out 0 (the reported bug). A freshly `AddVariable`'d field reflects immediately (the working
	 *  fresh-Gather path). Destructive-regen keeps no table data, so rebuilding a field loses nothing.
	 *  Add-before-remove keeps the UDS non-empty (RemoveVariable refuses the last member); RenameVariable
	 *  uniqueness is GUID-based, so transient duplicate friendly names during the swap are fine. */
	void SyncRowStructFields(UUserDefinedStruct* UDS, const TArray<FSeinBalanceColumn>& Desired)
	{
		if (!UDS || Desired.Num() == 0) return;

		auto FindByKey = [UDS](const FString& Key) -> FGuid
		{
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
			{
				const FString* Stored = FStructureEditorUtils::GetMetaData(UDS, V.VarGuid, SeinBalanceSourceKey);
				if (Stored && *Stored == Key) { return V.VarGuid; }
			}
			return FGuid();
		};

		TSet<FGuid> Keep;
		for (const FSeinBalanceColumn& D : Desired)
		{
			const FGuid ExistingGuid = FindByKey(D.SourceKey);
			if (ExistingGuid.IsValid())
			{
				if (const FStructVariableDescription* VD = FStructureEditorUtils::GetVarDescByGuid(UDS, ExistingGuid))
				{
					if (VD->FriendlyName == D.DisplayName && VD->ToPinType() == D.PinType)
					{
						Keep.Add(ExistingGuid);  // already correct — leave untouched (no churn)
						continue;
					}
				}
			}

			// Missing, or stale type/name: add it fresh with the correct pin type (auto-named, then
			// renamed + stamped). The old/stale field, if any, is removed below.
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
				Keep.Add(NewGuid);
			}
		}

		// Remove everything not kept or freshly added — stale-typed originals, strays, and the default
		// member from CreateUserDefinedStruct. The kept/added fields keep the UDS non-empty.
		TArray<FGuid> ToRemove;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(UDS))
		{
			if (!Keep.Contains(V.VarGuid)) { ToRemove.Add(V.VarGuid); }
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
	FNestedComponentColumnProvider NestedProvider;
	FAbilityFieldColumnProvider AbilityFieldProvider;
	FAbilityCostColumnProvider  AbilityCostProvider;

	TArray<FSeinBalanceColumn> Columns;
	if (Profile->TargetKind == ESeinBalanceTargetKind::Abilities)
	{
		AbilityFieldProvider.DescribeColumns(Targets, *Profile, Columns);
		AbilityCostProvider.DescribeColumns(Targets, *Profile, Columns);
	}
	else
	{
		ComponentProvider.DescribeColumns(Targets, *Profile, Columns);
		NestedProvider.DescribeColumns(Targets, *Profile, Columns);
		IdentityProvider.DescribeColumns(Targets, *Profile, Columns);
	}
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
		ProviderForKind(Col.Kind, ComponentProvider, IdentityProvider, NestedProvider, AbilityFieldProvider, AbilityCostProvider)
			.ReadInto(Target, Col, DestProp, DestPtr);
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

int32 PushToEntities(USeinBalanceProfile* Profile, int32& OutSkipped)
{
	OutSkipped = 0;
	if (!Profile) return -1;

	UDataTable* Table = Profile->GeneratedTable.LoadSynchronous();
	UUserDefinedStruct* RowUDS = Table ? Cast<UUserDefinedStruct>(Table->RowStruct) : nullptr;
	if (!Table || !RowUDS)
	{
		UE_LOG(LogSeinBalanceTable, Warning, TEXT("Push: '%s' has no generated table — Gather first."), *Profile->GetName());
		return -1;
	}

	// Rebuild the same column set Gather produced (deterministic for unchanged profile/targets).
	TArray<UClass*> Targets;
	Profile->ResolveTargetClasses(Targets);

	FComponentColumnProvider ComponentProvider;
	FIdentityColumnProvider  IdentityProvider;
	FNestedComponentColumnProvider NestedProvider;
	FAbilityFieldColumnProvider AbilityFieldProvider;
	FAbilityCostColumnProvider  AbilityCostProvider;
	TArray<FSeinBalanceColumn> Columns;
	if (Profile->TargetKind == ESeinBalanceTargetKind::Abilities)
	{
		AbilityFieldProvider.DescribeColumns(Targets, *Profile, Columns);
		AbilityCostProvider.DescribeColumns(Targets, *Profile, Columns);
	}
	else
	{
		ComponentProvider.DescribeColumns(Targets, *Profile, Columns);
		NestedProvider.DescribeColumns(Targets, *Profile, Columns);
		IdentityProvider.DescribeColumns(Targets, *Profile, Columns);
	}
	{
		TSet<FString> SeenKeys;
		Columns.RemoveAll([&SeenKeys](const FSeinBalanceColumn& C) { bool b = false; SeenKeys.Add(C.SourceKey, &b); return b; });
	}

	TMap<FName, UClass*> ByRow;
	for (UClass* T : Targets) { if (T) { ByRow.Add(RowNameForClass(T), T); } }

	// Writing into the Blueprints is destructive to their authored values — confirm.
	const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
		NSLOCTEXT("SeinBalanceTable", "PushWarn",
			"Push writes the values from '{0}' back into the source unit Blueprints, OVERWRITING their "
			"authored component values (only changed cells are written).\n\nContinue?"),
		FText::FromString(Table->GetName())));
	if (Choice != EAppReturnType::Yes) { return -1; }

	int32 NumValues = 0;
	TSet<UClass*> WrittenClasses;
	for (const FName& RowName : Table->GetRowNames())
	{
		UClass* const* Found = ByRow.Find(RowName);
		if (!Found || !*Found) { continue; }  // row's class no longer matched by the profile
		UClass* T = *Found;

		uint8* Row = Table->FindRowUnchecked(RowName);
		if (!Row) { continue; }

		bool bAny = false;
		for (const FSeinBalanceColumn& Col : Columns)
		{
			if (Col.Kind == ESeinBalanceColumnKind::Identity) { continue; }  // display-only — never pushed
			FProperty* CellProp = FindFieldByAuthoredName(RowUDS, Col.DisplayName);
			if (!CellProp) { continue; }
			const void* CellPtr = CellProp->ContainerPtrToValuePtr<void>(Row);
			switch (ProviderForKind(Col.Kind, ComponentProvider, IdentityProvider, NestedProvider, AbilityFieldProvider, AbilityCostProvider)
				.WriteFrom(T, Col, CellProp, CellPtr))
			{
			case ESeinBalanceWriteResult::Wrote:   ++NumValues; bAny = true; break;
			case ESeinBalanceWriteResult::Skipped: ++OutSkipped; break;
			default: break;  // Unchanged — silent (no perturbation)
			}
		}

		if (bAny)
		{
			WrittenClasses.Add(T);
			// Mirror the CDO edit to placed instances (the entity bridge exposes this for writes that
			// bypass a ComponentData-bearing property chain), then mark the BP modified so it persists.
			if (USeinEntityComponent* Bridge = FindBridge(T))
			{
				for (int32 i = 0; i < Bridge->ComponentData.Num(); ++i)
				{
					Bridge->PropagateComponentDataEntryToInstances(i);
				}
			}
			if (UBlueprint* BP = Cast<UBlueprint>(T->ClassGeneratedBy))
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			}
		}
	}

	UE_LOG(LogSeinBalanceTable, Log, TEXT("Push: '%s' → %d changed, %d skipped, across %d unit(s)."),
		*Profile->GetName(), NumValues, OutSkipped, WrittenClasses.Num());
	return NumValues;
}

int32 CheckSync(USeinBalanceProfile* Profile, int32& OutCellsChecked)
{
	OutCellsChecked = 0;
	if (!Profile) return -1;

	UDataTable* Table = Profile->GeneratedTable.LoadSynchronous();
	UUserDefinedStruct* RowUDS = Table ? Cast<UUserDefinedStruct>(Table->RowStruct) : nullptr;
	if (!Table || !RowUDS) return -1;

	TArray<UClass*> Targets;
	Profile->ResolveTargetClasses(Targets);

	FComponentColumnProvider ComponentProvider;
	FIdentityColumnProvider  IdentityProvider;
	FNestedComponentColumnProvider NestedProvider;
	FAbilityFieldColumnProvider AbilityFieldProvider;
	FAbilityCostColumnProvider  AbilityCostProvider;
	TArray<FSeinBalanceColumn> Columns;
	if (Profile->TargetKind == ESeinBalanceTargetKind::Abilities)
	{
		AbilityFieldProvider.DescribeColumns(Targets, *Profile, Columns);
		AbilityCostProvider.DescribeColumns(Targets, *Profile, Columns);
	}
	else
	{
		ComponentProvider.DescribeColumns(Targets, *Profile, Columns);
		NestedProvider.DescribeColumns(Targets, *Profile, Columns);
		IdentityProvider.DescribeColumns(Targets, *Profile, Columns);
	}

	TMap<FName, UClass*> ByRow;
	for (UClass* T : Targets) { if (T) { ByRow.Add(RowNameForClass(T), T); } }

	int32 Diffs = 0;
	for (const FName& RowName : Table->GetRowNames())
	{
		UClass* const* Found = ByRow.Find(RowName);
		if (!Found || !*Found) { continue; }
		UClass* T = *Found;
		uint8* Row = Table->FindRowUnchecked(RowName);
		if (!Row) { continue; }

		// Fresh buffer per row: fill it with the SOURCE values (via the same providers Gather uses),
		// then compare each tuning cell to the table's stored value. Identity columns are labels, skipped.
		uint8* Temp = static_cast<uint8*>(FMemory::Malloc(RowUDS->GetStructureSize()));
		RowUDS->InitializeStruct(Temp);
		for (const FSeinBalanceColumn& Col : Columns)
		{
			if (Col.Kind == ESeinBalanceColumnKind::Identity) { continue; }
			FProperty* Field = FindFieldByAuthoredName(RowUDS, Col.DisplayName);
			if (!Field) { continue; }

			// Honest-drift guard: a fixed→float column whose field isn't actually float/double means the
			// row struct is stale (e.g. not re-Gathered after a type change). Don't silently compare —
			// that previously read 0 == 0 as "in sync"; report it as out-of-sync so a re-Gather is prompted.
			if (Col.bConvertFixedToFloat && !CastField<FFloatProperty>(Field) && !CastField<FDoubleProperty>(Field))
			{
				++OutCellsChecked;
				++Diffs;
				continue;
			}

			void* TempPtr = Field->ContainerPtrToValuePtr<void>(Temp);
			ProviderForKind(Col.Kind, ComponentProvider, IdentityProvider, NestedProvider, AbilityFieldProvider, AbilityCostProvider)
				.ReadInto(T, Col, Field, TempPtr);
			++OutCellsChecked;
			if (!Field->Identical(TempPtr, Field->ContainerPtrToValuePtr<void>(Row))) { ++Diffs; }
		}
		RowUDS->DestroyStruct(Temp);
		FMemory::Free(Temp);
	}

	UE_LOG(LogSeinBalanceTable, Log, TEXT("CheckSync: '%s' → %d of %d cell(s) differ from source."),
		*Profile->GetName(), Diffs, OutCellsChecked);
	return Diffs;
}

}  // namespace SeinBalanceTable
