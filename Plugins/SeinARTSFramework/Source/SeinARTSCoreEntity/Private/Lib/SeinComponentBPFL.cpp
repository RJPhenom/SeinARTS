/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentBPFL.cpp
 */

#include "Lib/SeinComponentBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinBPFL (module-shared)

USeinWorldSubsystem* USeinComponentBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinComponentBPFL::SeinGetComponentData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, FInstancedStruct& OutData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !StructType || !Subsystem->IsEntityAlive(EntityHandle))
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: no subsystem or null struct type"));
		return false;
	}
	const ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(StructType);
	if (!Storage)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: no storage registered for %s"), *StructType->GetName());
		return false;
	}
	const void* Raw = Storage->GetComponentRaw(EntityHandle);
	if (!Raw)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("GetComponentData: entity %s has no %s"), *EntityHandle.ToString(), *StructType->GetName());
		return false;
	}
	OutData.InitializeAs(StructType, static_cast<const uint8*>(Raw));
	return true;
}

// =====================================================================
// CustomThunk-backed typed accessors (fronted by K2 nodes)
// =====================================================================
//
// Both thunks share a tight pattern:
//   1. The CustomStructureParam meta tells the engine the named parameter
//      is a wildcard struct — the K2 node connects it to the user-selected
//      type and the compiler emits stack-frame code matching that type.
//   2. In execXxx we step the stack with StepCompiledIn<FStructProperty>
//      (or MostRecentProperty) to pull the struct's UScriptStruct* and the
//      pointer to its bytes.
//   3. We dispatch to the world subsystem's storage path keyed by that
//      UScriptStruct* — identical to the templated AddComponent<T> /
//      GetComponent<T> walks.
//
// Returning false from a non-matching call leaves the typed pin untouched
// (BP graphs check the bool before reading). Determinism is preserved
// because storage access is the same single source of truth used by all
// other sim code.

bool USeinComponentBPFL::SeinGetComponentTyped(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, int32& OutStruct)
{
	// Direct call is never reached — the K2 node always generates a
	// CustomThunk-routed call that lands in execSeinGetComponentTyped below.
	// Body retained so the UFUNCTION reflection still picks up the signature.
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(USeinComponentBPFL::execSeinGetComponentTyped)
{
	// Stack layout matches the UFUNCTION signature: WorldContextObject,
	// EntityHandle, OutStruct (wildcard). Step each one off the script
	// frame in declaration order.
	P_GET_OBJECT(UObject, WorldContextObject);
	P_GET_STRUCT_REF(FSeinEntityHandle, EntityHandle);

	// Wildcard struct parameter — read the property + raw bytes from the stack.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	FStructProperty* OutProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* OutPtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	bool bResult = false;
	P_NATIVE_BEGIN;

	if (!OutProperty || !OutPtr || !OutProperty->Struct)
	{
		// No compiled-in struct type — the K2 node didn't supply a picker
		// selection. Leave OutStruct untouched; caller's bool branch handles.
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	USeinWorldSubsystem* Subsystem = USeinComponentBPFL::GetWorldSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}
	if (!Subsystem->IsEntityAlive(EntityHandle))
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	UScriptStruct* StructType = OutProperty->Struct;
	const ISeinComponentStorage* Storage = Subsystem->GetComponentStorageRaw(StructType);
	if (!Storage)
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	const void* Raw = Storage->GetComponentRaw(EntityHandle);
	if (!Raw)
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	// Copy storage bytes into the BP-side struct buffer. CopyScriptStruct
	// respects per-property semantics (TObjectPtr ref counting, FText
	// localization tables, FInstancedStruct deep copies, etc.) — a raw
	// memcpy would be incorrect for non-POD fields.
	StructType->CopyScriptStruct(OutPtr, Raw);
	bResult = true;

	P_NATIVE_END;

	*static_cast<bool*>(RESULT_PARAM) = bResult;
}

bool USeinComponentBPFL::SeinSetComponentTyped(const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle, const int32& InStruct)
{
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(USeinComponentBPFL::execSeinSetComponentTyped)
{
	P_GET_OBJECT(UObject, WorldContextObject);
	P_GET_STRUCT_REF(FSeinEntityHandle, EntityHandle);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	FStructProperty* InProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
	const void* InPtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	bool bResult = false;
	P_NATIVE_BEGIN;

	if (!InProperty || !InPtr || !InProperty->Struct)
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	USeinWorldSubsystem* Subsystem = USeinComponentBPFL::GetWorldSubsystem(WorldContextObject);
	if (!Subsystem
		|| !Subsystem->RequireStateMutationAuthorization(TEXT("SetComponentTyped"))
		|| !Subsystem->IsEntityAlive(EntityHandle))
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	UScriptStruct* StructType = InProperty->Struct;
	ISeinComponentStorage* Storage = Subsystem->GetOrCreateStorageForType(StructType);
	if (!Storage)
	{
		*static_cast<bool*>(RESULT_PARAM) = false;
		return;
	}

	// AddComponent overwrites if the entity already has this type — matches
	// the legacy templated AddComponent<T> semantics. K2 callers that need
	// to distinguish "added" vs "replaced" can call Get first.
	Storage->AddComponent(EntityHandle, InPtr);
	bResult = true;

	P_NATIVE_END;

	*static_cast<bool*>(RESULT_PARAM) = bResult;
}

TArray<FInstancedStruct> USeinComponentBPFL::SeinGetComponents(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle)
{
	TArray<FInstancedStruct> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->IsEntityAlive(EntityHandle)) return Result;

	// Sort component types by struct name before walking. TMap<UScriptStruct*>
	// iteration is pointer-keyed — stable within a process but not guaranteed
	// across clients. BP designers consuming this array can safely iterate
	// in order without accidentally introducing desyncs.
	TArray<UScriptStruct*> SortedTypes =
		Subsystem->GetComponentStorageTypes();
	SortedTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetFName().Compare(B.GetFName()) < 0;
	});

	for (UScriptStruct* StructType : SortedTypes)
	{
		const ISeinComponentStorage* Storage =
			Subsystem->GetComponentStorageRaw(StructType);
		if (!Storage) continue;
		if (const void* Raw =
			Storage->GetComponentRaw(EntityHandle))
		{
			FInstancedStruct Inst;
			Inst.InitializeAs(StructType, static_cast<const uint8*>(Raw));
			Result.Add(MoveTemp(Inst));
		}
	}
	return Result;
}
