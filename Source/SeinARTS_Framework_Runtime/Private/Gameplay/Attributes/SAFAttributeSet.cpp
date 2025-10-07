#include "Gameplay/Attributes/SAFAttributeSet.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "AttributeSet.h"

static bool IsGameplayAttributeDataStruct(const FStructProperty* StructProp) {
	return StructProp && StructProp->Struct && StructProp->Struct->GetFName() == TBaseStructure<FGameplayAttributeData>::Get()->GetFName();
}

bool USAFAttributeSet::TryGetNumericField(const UStruct* RowStruct, const uint8* RowData, const FName& FieldName, double& OutValue) {
	for (TFieldIterator<FProperty> It(RowStruct); It; ++It) {
		const FProperty* Prop = *It;
		if (Prop->GetFName() != FieldName) continue;
		if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop)) { OutValue = FloatProp->GetFloatingPointPropertyValue(Prop->ContainerPtrToValuePtr<void>(RowData)); return true; }
		if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop)) { OutValue = DoubleProp->GetFloatingPointPropertyValue(Prop->ContainerPtrToValuePtr<void>(RowData)); return true; }
		if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop)) { OutValue = static_cast<double>(IntProp->GetSignedIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(RowData))); return true; }
		if (const FInt64Property* I64Prop = CastField<FInt64Property>(Prop)) { OutValue = static_cast<double>(I64Prop->GetSignedIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(RowData))); return true; }
		if (const FUInt32Property* U32Prop = CastField<FUInt32Property>(Prop)) { OutValue = static_cast<double>(U32Prop->GetUnsignedIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(RowData))); return true; }
	}

	return false;
}

void USAFAttributeSet::SeedFromRowHandle(const FDataTableRowHandle& RowHandle, bool bMaintianMaxRatios) {
  if (!RowHandle.DataTable || RowHandle.RowName.IsNone()) return;

  // Get the row UScriptStruct from the DataTable (not from the handle).
  const UScriptStruct* RowStruct = RowHandle.DataTable->GetRowStruct();
  if (!RowStruct) return;

  // Get the raw row memory without a templated type.
  const TMap<FName, uint8*>& RowMap = RowHandle.DataTable->GetRowMap();
  const uint8* const* Found = RowMap.Find(RowHandle.RowName);
  const uint8* RowData = Found ? *Found : nullptr;
  if (!RowData) return;

  SeedFromUStruct(RowStruct, RowData, bMaintianMaxRatios);
}

void USAFAttributeSet::SeedFromUStruct(const UScriptStruct* RowStruct, const uint8* RowData, bool bMaintianMaxRatios) {
	TMap<FName, float> OldMaxByBaseName;
	TMap<FName, float> NewMaxByBaseName;

	// Pass 1: Apply Max* first, but remember old/new to support ratio maintenance.
	for (TFieldIterator<FProperty> It(GetClass()); It; ++It) {
		const FStructProperty* SP = CastField<FStructProperty>(*It);
		if (!IsGameplayAttributeDataStruct(SP)) continue;

		double NewMaxDouble = 0.0;
		const FName PropName = SP->GetFName();
		const FString PropStr = PropName.ToString();
		if (!PropStr.StartsWith(TEXT("Max"))) continue;
		if (!TryGetNumericField(RowStruct, RowData, PropName, NewMaxDouble)) continue;

		FGameplayAttributeData* Dest = SP->ContainerPtrToValuePtr<FGameplayAttributeData>(this);
		const float OldMax = Dest->GetCurrentValue();
		const float NewMax = static_cast<float>(NewMaxDouble);

		// Record before overwrite.
		const FName BaseName(*PropStr.RightChop(3));
		OldMaxByBaseName.Add(BaseName, OldMax);
		NewMaxByBaseName.Add(BaseName, NewMax);

		Dest->SetBaseValue(NewMax);
		Dest->SetCurrentValue(NewMax);
	}

	// Pass 2: Apply non-Max attributes.
	for (TFieldIterator<FProperty> It(GetClass()); It; ++It) {
		const FStructProperty* StructProp = CastField<FStructProperty>(*It);
		if (!IsGameplayAttributeDataStruct(StructProp)) continue;

		const FName PropName = StructProp->GetFName();
		const FString PropStr = PropName.ToString();
		if (PropStr.StartsWith(TEXT("Max"))) continue;

		FGameplayAttributeData* Dest = StructProp->ContainerPtrToValuePtr<FGameplayAttributeData>(this);

		double NewValDouble = 0.0;
		const bool bRowProvidesValue = TryGetNumericField(RowStruct, RowData, PropName, NewValDouble);

		// If row provides a value, set it (clamp to Max if present).
		if (bRowProvidesValue) {
			float NewVal = static_cast<float>(NewValDouble);

			// Clamp against Max* if known (from row) or can find current Max* property.
			const FName MaxName(*FString::Printf(TEXT("Max%s"), *PropStr));
			float MaxForClamp = TNumericLimits<float>::Max();

			if (const float* RowNewMax = NewMaxByBaseName.Find(PropName)) MaxForClamp = *RowNewMax;
			else {
				// Try read current Max* attribute on this set.
				if (FProperty* MaxProp = GetClass()->FindPropertyByName(MaxName)) {
					if (const FStructProperty* MaxSructProp = CastField<FStructProperty>(MaxProp)) {
						if (IsGameplayAttributeDataStruct(MaxSructProp)) {
							const FGameplayAttributeData* MaxData = MaxSructProp->ContainerPtrToValuePtr<FGameplayAttributeData>(this);
							MaxForClamp = MaxData->GetCurrentValue();
						}
					}
				}
			}

			if (FMath::IsFinite(MaxForClamp)) NewVal = FMath::Clamp(NewVal, 0.f, MaxForClamp);

			Dest->SetBaseValue(NewVal);
			Dest->SetCurrentValue(NewVal);
			continue;
		}

		// If row did not provide this value, but a paired Max* 
    // changed and we want to preserve ratio
		if (bMaintianMaxRatios) {
			const FName BaseName = PropName;
			if (const float* OldMax = OldMaxByBaseName.Find(BaseName)) {
				if (const float* NewMax = NewMaxByBaseName.Find(BaseName)) {
					const float OldCurr = Dest->GetCurrentValue();
					const float Ratio   = (*OldMax > 0.f) ? (OldCurr / *OldMax) : 1.f;
					const float NewCurr = FMath::Clamp(Ratio * (*NewMax), 0.f, *NewMax);
					Dest->SetBaseValue(NewCurr);
					Dest->SetCurrentValue(NewCurr);
				}
			}
		}
	}
}
