/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAttributeBPFL.cpp
 * @brief   Implementation of attribute resolution and modification Blueprint nodes.
 */

#include "Lib/SeinAttributeBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"

USeinWorldSubsystem* USeinAttributeBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

FFixedPoint USeinAttributeBPFL::SeinResolveAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !ComponentType) return FFixedPoint::Zero;
	return Subsystem->ResolveAttribute(EntityHandle, ComponentType, FieldName);
}

FFixedPoint USeinAttributeBPFL::SeinGetBaseAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !ComponentType) return FFixedPoint::Zero;

	ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage) return FFixedPoint::Zero;

	const void* ComponentData = Storage->GetComponentRaw(EntityHandle);
	if (!ComponentData) return FFixedPoint::Zero;

	FProperty* Prop = ComponentType->FindPropertyByName(FieldName);
	if (!Prop) return FFixedPoint::Zero;

	const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (StructProp && StructProp->Struct == FFixedPoint::StaticStruct())
	{
		return *StructProp->ContainerPtrToValuePtr<FFixedPoint>(ComponentData);
	}

	return FFixedPoint::Zero;
}

void USeinAttributeBPFL::SeinSetBaseAttribute(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* ComponentType, FName FieldName, FFixedPoint Value)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !ComponentType) return;

	ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(ComponentType);
	if (!Storage) return;

	void* ComponentData = Storage->GetComponentRaw(EntityHandle);
	if (!ComponentData) return;

	FProperty* Prop = ComponentType->FindPropertyByName(FieldName);
	if (!Prop) return;

	FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (StructProp && StructProp->Struct == FFixedPoint::StaticStruct())
	{
		*StructProp->ContainerPtrToValuePtr<FFixedPoint>(ComponentData) = Value;
	}
}
