/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementCanonicalStateProvider.cpp
 */

#include "Serialization/SeinMovementCanonicalStateProvider.h"
#include "Engine/World.h"

#include "Components/SeinMovementPayload.h"
#include "Core/SeinParallel.h"
#include "Movement/SeinAvoidance.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinMovement.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalDigestTree.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Serialization/SeinCanonicalStatePropertyPolicy.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinMovementCanonicalState.h"
#include "Serialization/SeinMovementStateCoverageInternal.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "Testing/SeinMovementCanonicalStateTestAccess.h"
#include "UObject/UnrealType.h"

struct FSeinMovementRoutineRootCache
{
	struct FEntry
	{
		uint64 Revision = 0;
		FGuid LeafDigest;
		uint64 PayloadBytes = 0;
	};

	uint64 TopologyRevision = 0;
	uint64 LatestMutationRevision = 0;
	TMap<FSeinEntityHandle, FEntry> Entries;
	FSeinCanonicalDigestTree Tree;
	FGuid AvoidanceDigest;
	uint64 AvoidanceRevision = 0;
	uint64 AvoidancePayloadBytes = 0;
	uint64 AggregatePayloadBytes = 0;
	FGuid CoverageDigest;
	FGuid SectionDigest;
};

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSMovement"));
	constexpr int32 MaxPolicyObjects = 262144;
	constexpr int32 MaxClassPathCharacters = 2048;
	constexpr int32 MaxObjectStateBytes = 1024 * 1024;
	constexpr int64 MaxAggregateStateBytes = 64ll * 1024ll * 1024ll;

	bool ShouldSkipStateProperty(const FProperty& Property)
	{
		return FSeinCanonicalStatePropertyPolicy::ShouldSkip(
			Property);
	}

	/**
	 * Tagged UObject state uses an opaque byte array in the outer canonical
	 * codec. Unordered containers would make those bytes depend on native hash
	 * layout, while instanced object references are not reconstructible from a
	 * path-only proxy archive. Such native layers must use Supplemental.
	 */
	bool ValidateReversibleProperty(
		const FProperty& Property,
		TSet<const UStruct*>& StructStack,
		FString& OutError,
		const FString& Path)
	{
		if (ShouldSkipStateProperty(Property))
		{
			return true;
		}
		if (Property.IsA<FSetProperty>()
			|| Property.IsA<FMapProperty>())
		{
			OutError = FString::Printf(
				TEXT("%s uses an unordered reflected container; declare Supplemental coverage with a canonical codec."),
				*Path);
			return false;
		}
		if (const FArrayProperty* Array =
			CastField<FArrayProperty>(&Property))
		{
			return ValidateReversibleProperty(
				*Array->Inner,
				StructStack,
				OutError,
				Path + TEXT("[]"));
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			return ValidateReversibleProperty(
				*Optional->GetValueProperty(),
				StructStack,
				OutError,
				Path + TEXT("?"));
		}
		if (const FObjectPropertyBase* Object =
			CastField<FObjectPropertyBase>(&Property))
		{
			if (Property.HasAnyPropertyFlags(
				CPF_InstancedReference | CPF_ContainsInstancedReference))
			{
				OutError = FString::Printf(
					TEXT("%s is an instanced UObject reference; declare Supplemental coverage with an exact object-graph codec."),
					*Path);
				return false;
			}
			return Object->PropertyClass != nullptr;
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				OutError = FString::Printf(
					TEXT("%s has no reflected struct type."), *Path);
				return false;
			}
			if (Struct == FInstancedStruct::StaticStruct())
			{
				OutError = FString::Printf(
					TEXT("%s has a dynamic FInstancedStruct schema; declare Supplemental coverage with a bounded dynamic codec."),
					*Path);
				return false;
			}
			if (StructStack.Contains(Struct))
			{
				return true;
			}
			StructStack.Add(Struct);
			for (TFieldIterator<FProperty> It(
				Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				if (!ValidateReversibleProperty(
					**It,
					StructStack,
					OutError,
					Path + TEXT(".") + It->GetName()))
				{
					StructStack.Remove(Struct);
					return false;
				}
			}
			StructStack.Remove(Struct);
		}
		return true;
	}

	bool ValidateReflectedClass(
		const UClass* Class,
		FGuid& OutSchemaDigest,
		FString& OutError)
	{
		OutSchemaDigest.Invalidate();
		if (!Class)
		{
			OutError = TEXT("Movement policy state has no class.");
			return false;
		}

		FSeinCanonicalReflectedStateLimits Limits;
		Limits.MaxAggregateElements = 1024 * 1024;
		Limits.MaxStringCharacters = 1024 * 1024;
		Limits.MaxTotalStringCharacters = 8 * 1024 * 1024;
		Limits.MaxRecursionDepth = 64;
		Limits.MaxInstancedObjects = 0;
		if (!FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
			Class, Limits, OutSchemaDigest, OutError))
		{
			return false;
		}

		TSet<const UStruct*> StructStack;
		StructStack.Add(Class);
		for (TFieldIterator<FProperty> It(
			Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			if (!ValidateReversibleProperty(
				**It,
				StructStack,
				OutError,
				Class->GetPathName() + TEXT(".") + It->GetName()))
			{
				return false;
			}
		}
		return true;
	}

	class FMovementStateProxyArchive final
		: public FObjectAndNameAsStringProxyArchive
	{
	public:
		FMovementStateProxyArchive(
			FArchive& Inner,
			bool bLoadIfFindFails)
			: FObjectAndNameAsStringProxyArchive(
				Inner, bLoadIfFindFails)
		{
			ArNoDelta = true;
		}

		virtual bool ShouldSkipProperty(
			const FProperty* Property) const override
		{
			return !Property
				|| FObjectAndNameAsStringProxyArchive::
					ShouldSkipProperty(Property)
				|| ShouldSkipStateProperty(*Property);
		}
	};

	FSeinCanonicalReflectedStateLimits ReflectedLimits()
	{
		FSeinCanonicalReflectedStateLimits Limits;
		Limits.MaxAggregateElements = 1024 * 1024;
		Limits.MaxStringCharacters = 1024 * 1024;
		Limits.MaxTotalStringCharacters = 8 * 1024 * 1024;
		Limits.MaxRecursionDepth = 64;
		Limits.MaxInstancedObjects = 0;
		return Limits;
	}

	bool SerializePolicyObject(
		const UObject& Object,
		FSeinMovementPolicyObjectState& OutState,
		FString& OutError,
		TMap<const UClass*, FGuid>* SchemaCache = nullptr)
	{
		OutState = {};
		const UClass* Class = Object.GetClass();
		if (!Class
			|| Class->HasAnyClassFlags(
				CLASS_Abstract | CLASS_Deprecated
					| CLASS_NewerVersionExists)
			|| !SeinValidateMovementStateCoverageForClass(
				Class, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Movement policy has an invalid exact class.");
			}
			return false;
		}

		OutState.ExactClassPath = Class->GetPathName();
		const FGuid* CachedSchema =
			SchemaCache ? SchemaCache->Find(Class) : nullptr;
		if (OutState.ExactClassPath.IsEmpty()
			|| OutState.ExactClassPath.Len()
				> MaxClassPathCharacters
			|| (!CachedSchema
				&& !ValidateReflectedClass(
					Class,
					OutState.ReflectedSchemaDigest,
					OutError)))
		{
			return false;
		}
		if (CachedSchema)
		{
			OutState.ReflectedSchemaDigest = *CachedSchema;
		}
		else if (SchemaCache)
		{
			SchemaCache->Add(
				Class, OutState.ReflectedSchemaDigest);
		}
		if (!FSeinCanonicalReflectedStateDigest::
				ComputeObjectValueDigest(
					&Object,
					OutState.ReflectedSchemaDigest,
					ReflectedLimits(),
					OutState.ReflectedValueDigest,
					OutError))
		{
			return false;
		}

		FMemoryWriter MemoryWriter(
			OutState.StateBytes, /*bIsPersistent*/ true);
		FMovementStateProxyArchive Writer(
			MemoryWriter, /*bLoadIfFindFails*/ false);
		Class->SerializeTaggedProperties(
			Writer,
			reinterpret_cast<uint8*>(
				const_cast<UObject*>(&Object)),
			Class,
			nullptr);
		if (Writer.IsError()
			|| MemoryWriter.IsError()
			|| MemoryWriter.Tell() != OutState.StateBytes.Num()
			|| OutState.StateBytes.Num() > MaxObjectStateBytes)
		{
			OutError = FString::Printf(
				TEXT("Movement policy '%s' failed bounded tagged-state serialization."),
				*OutState.ExactClassPath);
			OutState = {};
			return false;
		}
		return true;
	}

	bool ComputePolicyObjectLeafDigest(
		FStringView Kind,
		FSeinEntityHandle Handle,
		const FSeinMovementPolicyObjectState& State,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Movement.RoutinePolicyLeaf"), 1);
		return Writer.WriteString(FString(Kind))
			&& Writer.WriteInt32(Handle.Index)
			&& Writer.WriteInt32(Handle.Generation)
			&& Writer.WriteString(State.ExactClassPath)
			&& Writer.WriteGuid(State.ReflectedSchemaDigest)
			&& Writer.WriteGuid(State.ReflectedValueDigest)
			&& Writer.WriteBytes(State.StateBytes)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeMovementCoverageDigest(
		const FSeinMovementStateCoverageSnapshot& Coverage,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Movement.RoutineCoverage"), 1);
		if (!Writer.WriteName(Coverage.Identity)
			|| !Writer.WriteUInt32(
				static_cast<uint32>(Coverage.Claims.Num())))
		{
			return false;
		}
		for (const FString& Claim : Coverage.Claims)
		{
			if (!Writer.WriteString(Claim))
			{
				return false;
			}
		}
		if (!Writer.WriteUInt32(static_cast<uint32>(
			Coverage.SupplementalProviders.Num())))
		{
			return false;
		}
		for (const FSeinCanonicalStateKey& Key :
			Coverage.SupplementalProviders)
		{
			if (!Writer.WriteString(
				FSeinCanonicalStateRegistry::CanonicalKey(Key)))
			{
				return false;
			}
		}
		return Writer.Finalize(OutDigest, OutError);
	}

	bool ValidateExactClass(
		const FString& ClassPath,
		const UClass* RequiredBase,
		UClass*& OutClass,
		FString& OutError)
	{
		OutClass = nullptr;
		if (ClassPath.IsEmpty()
			|| ClassPath.Len() > MaxClassPathCharacters)
		{
			OutError = TEXT("Movement policy class path is invalid.");
			return false;
		}
		UClass* Loaded = LoadClass<UObject>(
			nullptr, *ClassPath, nullptr, LOAD_NoWarn);
		if (!Loaded
			|| Loaded->GetPathName() != ClassPath
			|| !Loaded->IsChildOf(RequiredBase)
			|| Loaded->HasAnyClassFlags(
				CLASS_Abstract | CLASS_Deprecated
					| CLASS_NewerVersionExists)
			|| !SeinValidateMovementStateCoverageForClass(
				Loaded, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Exact movement policy class '%s' is missing, redirected, incompatible, or abstract."),
					*ClassPath);
			}
			return false;
		}
		OutClass = Loaded;
		return true;
	}

	bool DeserializePolicyObject(
		const FSeinMovementPolicyObjectState& State,
		UObject& Object,
		FString& OutError,
		TMap<const UClass*, FGuid>* SchemaCache = nullptr)
	{
		if (Object.GetClass()->GetPathName()
				!= State.ExactClassPath
			|| !State.ReflectedSchemaDigest.IsValid()
			|| !State.ReflectedValueDigest.IsValid()
			|| State.StateBytes.Num() > MaxObjectStateBytes)
		{
			OutError =
				TEXT("Movement policy payload identity or bounds are invalid.");
			return false;
		}

		const FGuid* CachedSchema = SchemaCache
			? SchemaCache->Find(Object.GetClass())
			: nullptr;
		FGuid SchemaDigest = CachedSchema
			? *CachedSchema
			: FGuid();
		if ((!CachedSchema
				&& !ValidateReflectedClass(
					Object.GetClass(),
					SchemaDigest,
					OutError))
			|| SchemaDigest != State.ReflectedSchemaDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Movement policy reflected schema digest does not match the exact class.");
			}
			return false;
		}
		if (!CachedSchema && SchemaCache)
		{
			SchemaCache->Add(Object.GetClass(), SchemaDigest);
		}

		FMemoryReader MemoryReader(
			State.StateBytes, /*bIsPersistent*/ true);
		FMovementStateProxyArchive Reader(
			MemoryReader, /*bLoadIfFindFails*/ true);
		Object.GetClass()->SerializeTaggedProperties(
			Reader,
			reinterpret_cast<uint8*>(&Object),
			Object.GetClass(),
			nullptr);
		if (Reader.IsError()
			|| MemoryReader.IsError()
			|| MemoryReader.Tell() != State.StateBytes.Num())
		{
			OutError = FString::Printf(
				TEXT("Movement policy '%s' tagged-state payload is malformed or truncated."),
				*State.ExactClassPath);
			return false;
		}

		FSeinMovementPolicyObjectState RoundTrip;
		if (!SerializePolicyObject(
				Object, RoundTrip, OutError, SchemaCache)
			|| RoundTrip.ReflectedSchemaDigest
				!= State.ReflectedSchemaDigest
			|| RoundTrip.ReflectedValueDigest
				!= State.ReflectedValueDigest
			|| RoundTrip.StateBytes != State.StateBytes)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Movement policy payload is not an exact canonical tagged-state round trip.");
			}
			return false;
		}
		return true;
	}

	bool ValidateCoverageSnapshot(
		const FSeinMovementStateCoverageSnapshot& Expected,
		FString& OutError)
	{
		FSeinMovementStateCoverageSnapshot Current;
		if (!SeinBuildMovementStateCoverageSnapshot(
				Current, OutError))
		{
			return false;
		}
		if (!(Current == Expected))
		{
			OutError =
				TEXT("Movement state coverage changed after this canonical provider generation was registered.");
			return false;
		}
		return true;
	}

	bool ValidateAuthoredMovementClass(
		const FSeinMovementPayload& Component,
		const UClass* PayloadClass,
		FString& OutError)
	{
		UClass* AuthoredClass = nullptr;
		if (Component.MovementClass.IsNull())
		{
			AuthoredClass = USeinBasicMovement::StaticClass();
		}
		else
		{
			const FString AuthoredPath =
				Component.MovementClass.ToString();
			AuthoredClass = Component.MovementClass.
				TryLoadClass<USeinMovement>();
			if (!AuthoredClass
				|| AuthoredClass->GetPathName() != AuthoredPath
				|| AuthoredClass->HasAnyClassFlags(CLASS_Abstract))
			{
				OutError = FString::Printf(
					TEXT("Authored movement class '%s' is unavailable during exact restore."),
					*AuthoredPath);
				return false;
			}
		}
		if (AuthoredClass != PayloadClass)
		{
			OutError = FString::Printf(
				TEXT("Movement component class '%s' disagrees with payload class '%s'."),
				*AuthoredClass->GetPathName(),
				*PayloadClass->GetPathName());
			return false;
		}
		return true;
	}

	struct FMovementRestoreStage final
		: ISeinCanonicalStateRestoreStage
	{
		FSeinMovementStateCoverageSnapshot Coverage;
		TMap<FSeinEntityHandle, USeinMovement*> InstanceMap;
		TArray<TObjectPtr<USeinMovement>> InstancePool;
		USeinAvoidance* StagedAvoidance = nullptr;

		virtual void GatherReferencedObjects(
			TArray<UObject*>& OutObjects) const override
		{
			for (USeinMovement* Movement : InstancePool)
			{
				if (Movement)
				{
					OutObjects.Add(Movement);
				}
			}
			if (StagedAvoidance)
			{
				OutObjects.Add(StagedAvoidance);
			}
		}

		virtual bool VerifyExternalLeases(
			FString& OutError) const override
		{
			return ValidateCoverageSnapshot(
				Coverage, OutError);
		}
	};

	void CopyCanonicalProperties(
		const UObject& Source,
		UObject& Destination)
	{
		check(Source.GetClass() == Destination.GetClass());
		for (TFieldIterator<FProperty> It(
			Source.GetClass(),
			EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			if (!ShouldSkipStateProperty(**It))
			{
				It->CopyCompleteValue_InContainer(
					&Destination, &Source);
			}
		}
	}
}

bool SeinValidateMovementReflectedClassForCanonicalState(
	const UClass* Class,
	bool bRequireStatelessNativeLayer,
	FString& OutError)
{
	FGuid SchemaDigest;
	if (!ValidateReflectedClass(
		Class, SchemaDigest, OutError))
	{
		return false;
	}
	if (!bRequireStatelessNativeLayer)
	{
		return true;
	}

	for (TFieldIterator<FProperty> It(
		Class, EFieldIterationFlags::None); It; ++It)
	{
		if (!ShouldSkipStateProperty(**It))
		{
			OutError = FString::Printf(
				TEXT("Stateless native movement layer '%s' declares reflected state '%s'."),
				*Class->GetPathName(),
				*It->GetName());
			return false;
		}
	}
	return true;
}

struct FSeinMovementCanonicalStateProvider
{
	static bool Capture(
		const FSeinMovementStateCoverageSnapshot& Coverage,
		const FSeinCanonicalStateCaptureContext& Context,
		FInstancedStruct& OutState,
		FString& OutError)
	{
		OutState.Reset();
		if (!ValidateCoverageSnapshot(Coverage, OutError))
		{
			return false;
		}

		UWorld* UnrealWorld = Context.World.GetWorld();
		const USeinMovementSubsystem* Subsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinMovementSubsystem>()
				: nullptr;
		if (!Subsystem)
		{
			OutError =
				TEXT("Movement canonical capture could not resolve its world subsystem.");
			return false;
		}

		const TMap<FSeinEntityHandle, USeinMovement*>& InstanceMap =
			Subsystem->MovementInstanceMap;
		const TArray<TObjectPtr<USeinMovement>>& InstancePool =
			Subsystem->MovementInstancePool;
		if (InstanceMap.Num() != InstancePool.Num()
			|| InstanceMap.Num() > MaxPolicyObjects)
		{
			OutError =
				TEXT("Movement instance map/pool cardinality is invalid.");
			return false;
		}

		TSet<const USeinMovement*> Pooled;
		for (const USeinMovement* Movement : InstancePool)
		{
			if (!Movement
				|| Movement->GetOuter() != Subsystem
				|| Pooled.Contains(Movement))
			{
				OutError =
					TEXT("Movement instance pool contains a null, duplicate, or wrongly-owned object.");
				return false;
			}
			Pooled.Add(Movement);
		}

		TArray<FSeinEntityHandle> Handles;
		InstanceMap.GetKeys(Handles);
		Handles.Sort();

		FSeinMovementCanonicalState State;
		State.CoverageIdentity = Coverage.Identity;
		State.CoverageClaims = Coverage.Claims;
		State.MovementInstances.SetNum(Handles.Num());
		int64 AggregateBytes = 0;
		TMap<const UClass*, FGuid> SchemaCache;
		for (const USeinMovement* Movement : InstancePool)
		{
			const UClass* Class = Movement ? Movement->GetClass() : nullptr;
			if (Class && !SchemaCache.Contains(Class))
			{
				FGuid Schema;
				if (!ValidateReflectedClass(Class, Schema, OutError))
				{
					return false;
				}
				SchemaCache.Add(Class, Schema);
			}
		}
		if (Subsystem->AvoidanceInstance)
		{
			const UClass* Class =
				Subsystem->AvoidanceInstance->GetClass();
			if (Class && !SchemaCache.Contains(Class))
			{
				FGuid Schema;
				if (!ValidateReflectedClass(Class, Schema, OutError))
				{
					return false;
				}
				SchemaCache.Add(Class, Schema);
			}
		}

		TArray<const USeinMovement*> OrderedMovements;
		OrderedMovements.SetNum(Handles.Num());
		for (int32 Index = 0; Index < Handles.Num(); ++Index)
		{
			const FSeinEntityHandle Handle = Handles[Index];
			const USeinMovement* Movement =
				InstanceMap.FindRef(Handle);
			const FSeinMovementPayload* Component =
				Context.World.GetComponent<FSeinMovementPayload>(
					Handle);
			if (!Handle.IsValid()
				|| !Context.World.GetEntityPool().IsValid(Handle)
				|| !Movement
				|| !Pooled.Remove(Movement)
				|| !Component
				|| !ValidateAuthoredMovementClass(
					*Component,
					Movement->GetClass(),
					OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Movement instance map/pool/entity/component bijection is invalid.");
				}
				return false;
			}
			State.MovementInstances[Index].Entity = Handle;
			OrderedMovements[Index] = Movement;
		}
		if (!Pooled.IsEmpty())
		{
			OutError =
				TEXT("Movement instance pool contains an object absent from the handle map.");
			return false;
		}

		TArray<FString> CaptureErrors;
		CaptureErrors.SetNum(Handles.Num());
		SeinParallelFor(
			Handles.Num(),
			[&State,
			 &OrderedMovements,
			 &SchemaCache,
			 &CaptureErrors](const int32 Index)
			{
				SerializePolicyObject(
					*OrderedMovements[Index],
					State.MovementInstances[Index].Object,
					CaptureErrors[Index],
					&SchemaCache);
			});
		for (int32 Index = 0; Index < Handles.Num(); ++Index)
		{
			if (!CaptureErrors[Index].IsEmpty())
			{
				OutError = MoveTemp(CaptureErrors[Index]);
				return false;
			}
			const FSeinMovementPolicyObjectState& Object =
				State.MovementInstances[Index].Object;
			if (!Object.ReflectedValueDigest.IsValid())
			{
				OutError =
					TEXT("Movement policy capture produced no canonical value digest.");
				return false;
			}
			AggregateBytes += Object.StateBytes.Num();
			if (AggregateBytes > MaxAggregateStateBytes)
			{
				OutError =
					TEXT("Movement policy state exceeds its aggregate byte bound.");
				return false;
			}
		}

		if (Subsystem->AvoidanceInstance)
		{
			State.bHasAvoidance = true;
			if (Subsystem->AvoidanceInstance->GetOuter() != Subsystem
				|| !SerializePolicyObject(
					*Subsystem->AvoidanceInstance,
					State.Avoidance,
					OutError,
					&SchemaCache))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Avoidance singleton has invalid ownership or state.");
				}
				return false;
			}
			AggregateBytes += State.Avoidance.StateBytes.Num();
		}
		if (AggregateBytes > MaxAggregateStateBytes)
		{
			OutError =
				TEXT("Movement policy state exceeds its aggregate byte bound.");
			return false;
		}

		OutState = FInstancedStruct::Make(MoveTemp(State));
		return true;
	}

	static bool CaptureRoutineRoot(
		const FSeinMovementStateCoverageSnapshot& Coverage,
		const FSeinCanonicalStateCaptureContext& Context,
		bool bForceFullRebuild,
		FSeinCanonicalStateRoutineRootRecord& OutRecord,
		FString& OutError)
	{
		OutRecord = {};
		if (!ValidateCoverageSnapshot(Coverage, OutError))
		{
			return false;
		}
		UWorld* UnrealWorld = Context.World.GetWorld();
		USeinMovementSubsystem* Subsystem = UnrealWorld
			? UnrealWorld->GetSubsystem<USeinMovementSubsystem>()
			: nullptr;
		if (!Subsystem)
		{
			OutError =
				TEXT("Movement routine root could not resolve its world subsystem.");
			return false;
		}
		if (!Subsystem->RoutineRootCache.IsValid())
		{
			Subsystem->RoutineRootCache =
				MakeShared<FSeinMovementRoutineRootCache>();
			bForceFullRebuild = true;
		}
		FSeinMovementRoutineRootCache& Cache =
			*Subsystem->RoutineRootCache;
		if (!bForceFullRebuild
			&& Cache.SectionDigest.IsValid()
			&& Cache.TopologyRevision
				== Subsystem->MovementStateTopologyRevision
			&& Cache.LatestMutationRevision
				== Subsystem->MovementStateMutationRevision)
		{
			// Movement policy state is revision-gated. When neither policy
			// membership nor reflected policy state changed, the previously
			// sealed section digest is already the exact routine result.
			OutRecord.MutationRevision =
				Subsystem->MovementStateMutationRevision;
			OutRecord.ProjectedPayloadBytes = Cache.AggregatePayloadBytes;
			OutRecord.LeafDigest = Cache.SectionDigest;
			return true;
		}

		const TMap<FSeinEntityHandle, USeinMovement*>& InstanceMap =
			Subsystem->MovementInstanceMap;
		const TArray<TObjectPtr<USeinMovement>>& InstancePool =
			Subsystem->MovementInstancePool;
		if (InstanceMap.Num() != InstancePool.Num()
			|| InstanceMap.Num() > MaxPolicyObjects)
		{
			OutError =
				TEXT("Movement routine-root instance map/pool cardinality is invalid.");
			return false;
		}
		TSet<const USeinMovement*> Pooled;
		for (const USeinMovement* Movement : InstancePool)
		{
			if (!Movement
				|| Movement->GetOuter() != Subsystem
				|| Pooled.Contains(Movement))
			{
				OutError =
					TEXT("Movement routine-root pool contains a null, duplicate, or wrongly-owned object.");
				return false;
			}
			Pooled.Add(Movement);
		}

		TArray<FSeinEntityHandle> Handles;
		InstanceMap.GetKeys(Handles);
		Handles.Sort();
		TSet<FSeinEntityHandle> CurrentHandles;
		CurrentHandles.Reserve(Handles.Num());
		for (const FSeinEntityHandle Handle : Handles)
		{
			CurrentHandles.Add(Handle);
		}
		const int32 SlotCount =
			Context.World.GetEntityPool().GetCapacity() + 1;
		const bool bTreeReset = bForceFullRebuild
			|| Cache.Tree.Num() != SlotCount;
		if (bTreeReset
			&& !Cache.Tree.Reset(
				TEXT("movement.policy-instances"),
				SlotCount,
				OutError))
		{
			return false;
		}
		if (bForceFullRebuild)
		{
			Cache.Entries.Reset();
			Cache.AvoidanceDigest.Invalidate();
			Cache.AvoidanceRevision = 0;
			Cache.CoverageDigest.Invalidate();
		}

		for (auto It = Cache.Entries.CreateIterator(); It; ++It)
		{
			if (CurrentHandles.Contains(It->Key))
			{
				continue;
			}
			if (!bTreeReset
				&& !Cache.Tree.SetLeafDigest(
					It->Key.Index, FGuid(), OutError))
			{
				return false;
			}
			It.RemoveCurrent();
		}

		uint64 AggregateBytes = 0;
		TMap<const UClass*, FGuid> SchemaCache;
		for (const FSeinEntityHandle Handle : Handles)
		{
			USeinMovement* Movement = InstanceMap.FindRef(Handle);
			const FSeinMovementPayload* Component =
				Context.World.GetComponent<FSeinMovementPayload>(Handle);
			if (!Handle.IsValid()
				|| !Context.World.GetEntityPool().IsValid(Handle)
				|| !Movement
				|| !Pooled.Remove(Movement)
				|| !Component
				|| !ValidateAuthoredMovementClass(
					*Component, Movement->GetClass(), OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Movement routine-root map/pool/entity/component bijection is invalid.");
				}
				return false;
			}

			const uint64 Revision =
				Subsystem->MovementStateRevisions.FindRef(Handle);
			FSeinMovementRoutineRootCache::FEntry* Existing =
				Cache.Entries.Find(Handle);
			if (!Existing || Existing->Revision != Revision)
			{
				FSeinMovementPolicyObjectState State;
				FGuid LeafDigest;
				if (!SerializePolicyObject(
					*Movement, State, OutError, &SchemaCache)
					|| !ComputePolicyObjectLeafDigest(
						TEXT("movement"),
						Handle,
						State,
						LeafDigest,
						OutError))
				{
					return false;
				}
				FSeinMovementRoutineRootCache::FEntry Entry;
				Entry.Revision = Revision;
				Entry.LeafDigest = LeafDigest;
				Entry.PayloadBytes = State.StateBytes.Num();
				Existing = &Cache.Entries.Add(Handle, MoveTemp(Entry));
			}
			if (!Cache.Tree.SetLeafDigest(
				Handle.Index, Existing->LeafDigest, OutError))
			{
				return false;
			}
			AggregateBytes += Existing->PayloadBytes;
		}
		if (!Pooled.IsEmpty()
			|| !Cache.Tree.FinalizeUpdates(OutError))
		{
			return !Pooled.IsEmpty()
				? (OutError =
					TEXT("Movement routine-root pool contains an object absent from its handle map."),
					false)
				: false;
		}

		if (Subsystem->AvoidanceInstance)
		{
			if (Subsystem->AvoidanceInstance->GetOuter() != Subsystem)
			{
				OutError =
					TEXT("Movement avoidance singleton has invalid ownership.");
				return false;
			}
			if (!Cache.AvoidanceDigest.IsValid()
				|| Cache.AvoidanceRevision
					!= Subsystem->AvoidanceStateRevision)
			{
				FSeinMovementPolicyObjectState State;
				if (!SerializePolicyObject(
					*Subsystem->AvoidanceInstance,
					State,
					OutError,
					&SchemaCache)
					|| !ComputePolicyObjectLeafDigest(
						TEXT("avoidance"),
						FSeinEntityHandle(),
						State,
						Cache.AvoidanceDigest,
						OutError))
				{
					return false;
				}
				Cache.AvoidanceRevision =
					Subsystem->AvoidanceStateRevision;
				Cache.AvoidancePayloadBytes = State.StateBytes.Num();
			}
			AggregateBytes += Cache.AvoidancePayloadBytes;
		}
		else
		{
			Cache.AvoidanceDigest.Invalidate();
			Cache.AvoidancePayloadBytes = 0;
		}
		if (AggregateBytes > MaxAggregateStateBytes)
		{
			OutError =
				TEXT("Movement routine-root state exceeds its aggregate byte bound.");
			return false;
		}
		if (!Cache.CoverageDigest.IsValid()
			&& !ComputeMovementCoverageDigest(
				Coverage, Cache.CoverageDigest, OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Movement.RoutineRoot"), 1);
		if (!Writer.WriteGuid(Cache.CoverageDigest)
			|| !Writer.WriteInt32(Handles.Num())
			|| !Writer.WriteGuid(Cache.Tree.GetRoot())
			|| !Writer.WriteBool(Subsystem->AvoidanceInstance != nullptr)
			|| (Subsystem->AvoidanceInstance
				&& !Writer.WriteGuid(Cache.AvoidanceDigest))
			|| !Writer.Finalize(Cache.SectionDigest, OutError))
		{
			return false;
		}
		Cache.TopologyRevision =
			Subsystem->MovementStateTopologyRevision;
		Cache.LatestMutationRevision =
			Subsystem->MovementStateMutationRevision;
		Cache.AggregatePayloadBytes = AggregateBytes;
		OutRecord.MutationRevision =
			Subsystem->MovementStateMutationRevision;
		OutRecord.ProjectedPayloadBytes = AggregateBytes;
		OutRecord.LeafDigest = Cache.SectionDigest;
		return true;
	}

	static bool StageRestore(
		const FSeinMovementStateCoverageSnapshot& Coverage,
		const FSeinCanonicalStateStageContext& Context,
		const FInstancedStruct& State,
		TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
		FString& OutError)
	{
		OutStage.Reset();
		const FSeinMovementCanonicalState* Payload =
			State.GetPtr<FSeinMovementCanonicalState>();
		if (!Payload
			|| Payload->CoverageIdentity != Coverage.Identity
			|| Payload->CoverageClaims != Coverage.Claims
			|| !Context.Candidate
			|| !Context.Services
			|| !ValidateCoverageSnapshot(Coverage, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Movement canonical payload, coverage identity, or restore context is invalid.");
			}
			return false;
		}
		if (Payload->MovementInstances.Num() > MaxPolicyObjects)
		{
			OutError =
				TEXT("Movement canonical payload exceeds its object-count bound.");
			return false;
		}

		UWorld* UnrealWorld = Context.Services->GetWorld();
		USeinMovementSubsystem* Subsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinMovementSubsystem>()
				: nullptr;
		if (!Subsystem)
		{
			OutError =
				TEXT("Movement restore staging could not resolve its final object outer.");
			return false;
		}

		TUniquePtr<FMovementRestoreStage> Stage =
			MakeUnique<FMovementRestoreStage>();
		Stage->Coverage = Coverage;
		Stage->InstancePool.Reserve(
			Payload->MovementInstances.Num());

		FSeinEntityHandle Previous;
		int64 AggregateBytes = 0;
		TMap<const UClass*, FGuid> SchemaCache;
		for (int32 Index = 0;
			Index < Payload->MovementInstances.Num();
			++Index)
		{
			const FSeinMovementPolicyInstanceState& Record =
				Payload->MovementInstances[Index];
			if (!Record.Entity.IsValid()
				|| (Index > 0 && !(Previous < Record.Entity))
				|| !Context.Candidate->IsEntityValid(Record.Entity))
			{
				OutError =
					TEXT("Movement records require valid unique handles in strict canonical order.");
				return false;
			}
			Previous = Record.Entity;

			const FSeinMovementPayload* Component =
				Context.Candidate->
					FindComponent<FSeinMovementPayload>(
						Record.Entity);
			UClass* ExactClass = nullptr;
			if (!Component
				|| !ValidateExactClass(
					Record.Object.ExactClassPath,
					USeinMovement::StaticClass(),
					ExactClass,
					OutError)
				|| !ValidateAuthoredMovementClass(
					*Component, ExactClass, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Movement restore record has no matching component or exact class.");
				}
				return false;
			}

			USeinMovement* Movement =
				NewObject<USeinMovement>(Subsystem, ExactClass);
			if (!Movement)
			{
				OutError =
					TEXT("Movement restore staging failed to allocate an exact policy instance.");
				return false;
			}
			Movement->HydrateTuningFromData(
				Component->MovementClassData);
			if (!DeserializePolicyObject(
				Record.Object,
				*Movement,
				OutError,
				&SchemaCache))
			{
				return false;
			}
			Stage->InstanceMap.Add(Record.Entity, Movement);
			Stage->InstancePool.Add(Movement);
			AggregateBytes += Record.Object.StateBytes.Num();
			if (AggregateBytes > MaxAggregateStateBytes)
			{
				OutError =
					TEXT("Movement restore payload exceeds its aggregate byte bound.");
				return false;
			}
		}

		if (Payload->bHasAvoidance)
		{
			UClass* ExactClass = nullptr;
			if (!Subsystem->AvoidanceInstance
				|| !ValidateExactClass(
					Payload->Avoidance.ExactClassPath,
					USeinAvoidance::StaticClass(),
					ExactClass,
					OutError)
				|| Subsystem->AvoidanceInstance->GetClass()
					!= ExactClass)
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Avoidance payload does not match the initialized destination singleton.");
				}
				return false;
			}
			Stage->StagedAvoidance =
				NewObject<USeinAvoidance>(Subsystem, ExactClass);
			if (!Stage->StagedAvoidance
				|| !DeserializePolicyObject(
					Payload->Avoidance,
					*Stage->StagedAvoidance,
					OutError,
					&SchemaCache))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Avoidance singleton restore staging failed.");
				}
				return false;
			}
			AggregateBytes += Payload->Avoidance.StateBytes.Num();
		}
		else if (Subsystem->AvoidanceInstance)
		{
			OutError =
				TEXT("Avoidance-off payload disagrees with the initialized destination world.");
			return false;
		}
		if (AggregateBytes > MaxAggregateStateBytes)
		{
			OutError =
				TEXT("Movement restore payload exceeds its aggregate byte bound.");
			return false;
		}

		OutStage = MoveTemp(Stage);
		return true;
	}

	static void CommitRestore(
		FSeinCanonicalStateCommitContext& Context,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&& OpaqueStage)
	{
		FMovementRestoreStage* Stage =
			static_cast<FMovementRestoreStage*>(
				OpaqueStage.Get());
		check(Stage);
		UWorld* UnrealWorld = Context.World.GetWorld();
		USeinMovementSubsystem* Subsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinMovementSubsystem>()
				: nullptr;
		check(Subsystem);

		Subsystem->MovementInstanceMap =
			MoveTemp(Stage->InstanceMap);
		Subsystem->MovementInstancePool =
			MoveTemp(Stage->InstancePool);
		Subsystem->MovementStateRevisions.Reset();
		for (const auto& Pair : Subsystem->MovementInstanceMap)
		{
			Subsystem->MarkMovementStateDirty(Pair.Key);
		}
		Subsystem->BumpMovementTopologyRevision();
		Subsystem->RoutineRootCache.Reset();

		if (Stage->StagedAvoidance)
		{
			check(Subsystem->AvoidanceInstance);
			check(Subsystem->AvoidanceInstance->GetClass()
				== Stage->StagedAvoidance->GetClass());
			CopyCanonicalProperties(
				*Stage->StagedAvoidance,
				*Subsystem->AvoidanceInstance);
		}
	}
};

FSeinCanonicalStateRegistrationHandle
SeinRegisterMovementCanonicalStateProvider(
	const FSeinMovementStateCoverageSnapshot& Coverage,
	FString& OutError)
{
	if (Coverage.Identity.IsNone())
	{
		OutError =
			TEXT("Movement canonical provider requires a valid coverage identity.");
		return {};
	}

	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId =
		TEXT("seinarts.movement");
	Descriptor.Key.StableContributorId =
		TEXT("persistent-policy-instances");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 2;
	Descriptor.Role = ESeinCanonicalStateRole::Continuation;
	Descriptor.PayloadStruct =
		FSeinMovementCanonicalState::StaticStruct();
	Descriptor.AllowedNames = { Coverage.Identity };
	Descriptor.RestoreAfter = Coverage.SupplementalProviders;
	Descriptor.Limits.MaxRecursionDepth = 64;
	Descriptor.Limits.MaxEncodedBytes =
		static_cast<int32>(MaxAggregateStateBytes);
	Descriptor.Limits.MaxAggregateElements =
		4 * 1024 * 1024;

	FSeinCanonicalStateContributorOps Ops;
	Ops.Capture =
		[Coverage](
			const FSeinCanonicalStateCaptureContext& Context,
			FInstancedStruct& OutState,
			FString& Error)
		{
			return FSeinMovementCanonicalStateProvider::Capture(
				Coverage, Context, OutState, Error);
		};
	Ops.CaptureRoutineRoot =
		[Coverage](
			const FSeinCanonicalStateCaptureContext& Context,
			bool bForceFullRebuild,
			FSeinCanonicalStateRoutineRootRecord& OutRecord,
			FString& Error)
		{
			return FSeinMovementCanonicalStateProvider::
				CaptureRoutineRoot(
					Coverage,
					Context,
					bForceFullRebuild,
					OutRecord,
					Error);
		};
	Ops.StageRestore =
		[Coverage](
			const FSeinCanonicalStateStageContext& Context,
			const FInstancedStruct& State,
			TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
			FString& Error)
		{
			return FSeinMovementCanonicalStateProvider::StageRestore(
				Coverage, Context, State, OutStage, Error);
		};
	Ops.CommitRestore =
		&FSeinMovementCanonicalStateProvider::CommitRestore;

	return FSeinCanonicalStateRegistry::Register(
		OwnerModuleId,
		Descriptor,
		MoveTemp(Ops),
		&OutError);
}

#if WITH_DEV_AUTOMATION_TESTS

bool SeinReplaceFirstMovementClassPathForTest(
	FSeinCanonicalStateContributorRecord& Record,
	const FString& ReplacementClassPath,
	FString& OutError)
{
	OutError.Reset();
	if (Record.Key.StableDomainId
			!= FName(TEXT("seinarts.movement"))
		|| Record.Key.StableContributorId
			!= FName(TEXT("persistent-policy-instances"))
		|| ReplacementClassPath.IsEmpty()
		|| ReplacementClassPath.Len()
			> MaxClassPathCharacters)
	{
		OutError =
			TEXT("Movement test mutation requires the exact provider record and a bounded replacement path.");
		return false;
	}

	FSeinMovementStateCoverageSnapshot Coverage;
	if (!SeinBuildMovementStateCoverageSnapshot(
			Coverage, OutError))
	{
		return false;
	}

	FSeinStructWireLimits Limits;
	Limits.MaxBytes =
		static_cast<int32>(MaxAggregateStateBytes);
	Limits.MaxAggregateElements = 4 * 1024 * 1024;
	Limits.MaxStringBytes = 1024 * 1024;
	Limits.MaxRecursionDepth = 64;
	Limits.MaxNativeAllocationBytes = Limits.MaxBytes;
	const TArray<FName> AllowedNames = { Coverage.Identity };
	const TArray<const UScriptStruct*> DynamicStructs;

	FSeinMovementCanonicalState Payload;
	FSeinWireCost Cost;
	if (!FSeinCanonicalStateCodec::DecodeWithCost(
			Record.PayloadBytes,
			FSeinMovementCanonicalState::StaticStruct(),
			&Payload,
			{ DynamicStructs, AllowedNames },
			Limits,
			OutError,
			Cost))
	{
		return false;
	}
	if (!Payload.MovementInstances.IsEmpty())
	{
		Payload.MovementInstances[0].Object.ExactClassPath =
			ReplacementClassPath;
	}
	else if (Payload.bHasAvoidance)
	{
		Payload.Avoidance.ExactClassPath =
			ReplacementClassPath;
	}
	else
	{
		OutError =
			TEXT("Movement test mutation requires at least one policy object.");
		return false;
	}

	TArray<uint8> MutatedBytes;
	if (!FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinMovementCanonicalState::StaticStruct(),
			&Payload,
			{ DynamicStructs, AllowedNames },
			Limits,
			MutatedBytes,
			OutError,
			Cost))
	{
		return false;
	}

	FGuid LeafDigest;
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.CanonicalState.Leaf"), 1);
	if (!Writer.WriteGuid(Record.DescriptorDigest)
		|| !Writer.WriteBytes(MutatedBytes)
		|| !Writer.Finalize(LeafDigest, OutError))
	{
		return false;
	}
	Record.PayloadBytes = MoveTemp(MutatedBytes);
	Record.LeafDigest = LeafDigest;
	return true;
}

#endif
