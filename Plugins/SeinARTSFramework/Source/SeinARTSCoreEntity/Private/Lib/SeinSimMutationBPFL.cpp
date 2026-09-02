/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimMutationBPFL.cpp
 * @brief   Implementation of the restricted sim-mutation BPFL.
 */

#include "Lib/SeinSimMutationBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinEntityPool.h"
#include "Types/Entity.h"
#include "Math/MathLib.h"
#include "Components/SeinCommandBrokerData.h"
#include "Events/SeinVisualEvent.h"
#include "Attributes/SeinAttributeResolver.h"

#include "SeinARTSCoreEntityLog.h"  // LogSeinBPFL (module-shared)

USeinWorldSubsystem* USeinSimMutationBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

// Templated helper: whole-struct write. Bails on invalid handle / missing storage.
namespace
{
	template<typename T>
	bool WriteWholeStruct(const UObject* WorldContextObject, FSeinEntityHandle Handle, const T& NewData, const TCHAR* FnName)
	{
		UWorld* World = WorldContextObject ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		USeinWorldSubsystem* Subsystem = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("%s: no SeinWorldSubsystem"), FnName);
			return false;
		}
		if (!Subsystem->RequireStateMutationAuthorization(FnName)) return false;
		T* Dst = Subsystem->GetComponentMutable<T>(Handle);
		if (!Dst)
		{
			UE_LOG(LogSeinBPFL, Warning, TEXT("%s: entity %s invalid or has no %s"), FnName, *Handle.ToString(), *T::StaticStruct()->GetName());
			return false;
		}
		*Dst = NewData;
		return true;
	}
}

bool USeinSimMutationBPFL::SeinSetAbilityData(const UObject* WCO, FSeinEntityHandle H, const FSeinAbilityPayload& D)       { return WriteWholeStruct(WCO, H, D, TEXT("SetAbilityData")); }
bool USeinSimMutationBPFL::SeinSetProductionData(const UObject* WCO, FSeinEntityHandle H, const FSeinProductionPayload& D) { return WriteWholeStruct(WCO, H, D, TEXT("SetProductionData")); }

bool USeinSimMutationBPFL::SeinSetComponent(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, UScriptStruct* StructType, const FInstancedStruct& NewData)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: no subsystem or null struct type"));
		return false;
	}
	if (!Subsystem->RequireStateMutationAuthorization(TEXT("SetComponent"))) return false;
	if (NewData.GetScriptStruct() != StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: NewData type %s doesn't match requested %s"),
			NewData.GetScriptStruct() ? *NewData.GetScriptStruct()->GetName() : TEXT("<null>"),
			*StructType->GetName());
		return false;
	}
	ISeinComponentStorage* Storage =
		Subsystem->GetComponentStorageMutable(StructType);
	if (!Storage)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: no storage registered for %s"), *StructType->GetName());
		return false;
	}
	void* Dst = Storage->GetComponentRaw(EntityHandle);
	if (!Dst)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("SetComponent: entity %s has no %s"), *EntityHandle.ToString(), *StructType->GetName());
		return false;
	}
	StructType->CopyScriptStruct(Dst, NewData.GetMemory());
	return true;
}

bool USeinSimMutationBPFL::SeinApplyFieldDelta(
	const UObject* WorldContextObject, FSeinEntityHandle EntityHandle,
	UScriptStruct* StructType, FName FieldName, FFixedPoint Delta,
	bool bClampMin, FFixedPoint MinValue, bool bClampMax, FFixedPoint MaxValue,
	FFixedPoint& NewValue, bool& bChanged, bool& bAtMin, bool& bAtMax)
{
	NewValue = FFixedPoint::Zero;
	bChanged = false;
	bAtMin = false;
	bAtMax = false;

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem || !StructType)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("ApplyFieldDelta: no subsystem or null struct type"));
		return false;
	}
	if (!Subsystem->RequireStateMutationAuthorization(TEXT("ApplyFieldDelta"))) return false;
	if (bClampMin && bClampMax && MinValue > MaxValue)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("ApplyFieldDelta: MinValue exceeds MaxValue for %s.%s"),
			*StructType->GetName(), *FieldName.ToString());
		return false;
	}
	if (!Subsystem->IsEntityAlive(EntityHandle))
	{
		return false;
	}
	// Resolve the field once. Internal name first, then the authored UDS name
	// (FindFieldProperty handles both); only FFixedPoint fields are eligible.
	FProperty* Property = FSeinAttributeResolver::FindFieldProperty(StructType, FieldName);
	if (!FSeinAttributeResolver::IsFixedPointField(Property))
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("ApplyFieldDelta: %s has no fixed-point field named %s"),
			*StructType->GetName(), *FieldName.ToString());
		return false;
	}

	// Take the mutable storage FIRST so every gate (including the read-only-
	// callback gate behind GetComponentStorageMutable) fails before any output
	// is published. The read itself goes through the const accessor so a no-op
	// never advances the component's mutation revision; only the actual write
	// below uses the publishing mutable GetComponentRaw.
	ISeinComponentStorage* Storage = Subsystem->GetComponentStorageMutable(StructType);
	const void* ConstData = Storage
		? static_cast<const ISeinComponentStorage*>(Storage)->GetComponentRaw(EntityHandle)
		: nullptr;
	if (!ConstData)
	{
		UE_LOG(LogSeinBPFL, Warning, TEXT("ApplyFieldDelta: entity %s has no %s"),
			*EntityHandle.ToString(), *StructType->GetName());
		return false;
	}
	const FFixedPoint Current = *Property->ContainerPtrToValuePtr<FFixedPoint>(ConstData);

	// Saturating add on the raw 64-bit representation — FFixedPoint's own
	// operator+ wraps by contract, which is exactly wrong for a stat delta.
	const int64 A = Current.Value;
	const int64 B = Delta.Value;
	int64 Sum;
	if (B > 0 && A > MAX_int64 - B)
	{
		Sum = MAX_int64;
	}
	else if (B < 0 && A < MIN_int64 - B)
	{
		Sum = MIN_int64;
	}
	else
	{
		Sum = A + B;
	}
	FFixedPoint Result(Sum);
	if (bClampMin && Result < MinValue) Result = MinValue;
	if (bClampMax && Result > MaxValue) Result = MaxValue;

	NewValue = Result;
	bAtMin = bClampMin && Result == MinValue;
	bAtMax = bClampMax && Result == MaxValue;
	if (Result == Current)
	{
		return true;
	}

	void* Data = Storage->GetComponentRaw(EntityHandle);
	check(Data); // same storage, same handle, same tick — the const read just succeeded
	*Property->ContainerPtrToValuePtr<FFixedPoint>(Data) = Result;
	bChanged = true;
	return true;
}

// ─── Movement field-level (removed) ───
// See SeinSimMutationBPFL.h for the rationale. Designers use the generic
// K2Node_SeinSetComponent for FSeinMovementPayload / FSeinNavigationPayload
// field mutation.

// ─── Production field-level ───

bool USeinSimMutationBPFL::SeinSetRallyPoint(const UObject* WCO, FSeinEntityHandle H, FFixedVector V)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetRallyPoint"))) return false;
	FSeinProductionPayload* D =
		S->GetComponentMutable<FSeinProductionPayload>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetRallyPoint: entity %s has no FSeinProductionPayload"), *H.ToString()); return false; }
	D->bRallyToEntity = false;
	// Identity rotation — this legacy mutation BPFL takes a location only.
	// Designers wanting facing should call USeinProductionBPFL::SeinSetRallyPoint(Transform).
	D->RallyTransform = FFixedTransform(V);
	D->RallyEntity = FSeinEntityHandle();
	return true;
}

bool USeinSimMutationBPFL::SeinGrantPairCapability(const UObject* WCO,
	FSeinPlayerID SourcePlayer, FSeinPlayerID TargetPlayer,
	FGameplayTag CapabilityTag, FGameplayTag SourceKindTag,
	int64 SourceInstanceID)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("GrantPairCapability")))
	{
		return false;
	}
	return S->GrantPairCapability(SourcePlayer, TargetPlayer,
		CapabilityTag, SourceKindTag, SourceInstanceID);
}

bool USeinSimMutationBPFL::SeinRevokePairCapability(const UObject* WCO,
	FSeinPlayerID SourcePlayer, FSeinPlayerID TargetPlayer,
	FGameplayTag CapabilityTag, FGameplayTag SourceKindTag,
	int64 SourceInstanceID)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("RevokePairCapability")))
	{
		return false;
	}
	return S->RevokePairCapability(SourcePlayer, TargetPlayer,
		CapabilityTag, SourceKindTag, SourceInstanceID);
}

bool USeinSimMutationBPFL::SeinSetCurrentBuildProgress(const UObject* WCO, FSeinEntityHandle H, FFixedPoint V)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetCurrentBuildProgress"))) return false;
	FSeinProductionPayload* D =
		S->GetComponentMutable<FSeinProductionPayload>(H);
	if (!D) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetCurrentBuildProgress: entity %s has no FSeinProductionPayload"), *H.ToString()); return false; }
	D->CurrentBuildProgress = V;
	return true;
}

// Squad mutations moved to USeinSquadMutationBPFL in the SeinARTSSquad module.

// ─── Child transforms field-level ───

namespace
{
	constexpr int32 MaxParentChainDepthMutation = 32;

	/** Linear find by tag (mutable). */
	FSeinChildTransform* FindByTagMutable(TArray<FSeinChildTransform>& Nodes, FGameplayTag Tag)
	{
		for (FSeinChildTransform& N : Nodes)
		{
			if (N.Tag == Tag) return &N;
		}
		return nullptr;
	}

	/** Linear find by tag (const). */
	const FSeinChildTransform* FindByTagConst(const TArray<FSeinChildTransform>& Nodes, FGameplayTag Tag)
	{
		for (const FSeinChildTransform& N : Nodes)
		{
			if (N.Tag == Tag) return &N;
		}
		return nullptr;
	}

	/** Compose the world transform of `Leaf`'s IMMEDIATE PARENT — used by
	 *  TurnChildToward to back-solve a desired world rotation into local
	 *  space. Returns EntityWorld if Leaf is a root-level child. */
	FFixedTransform ComposeParentWorld(
		const TArray<FSeinChildTransform>& Nodes,
		const FSeinChildTransform* Leaf,
		const FFixedTransform& EntityWorld)
	{
		if (!Leaf->ParentTag.IsValid()) return EntityWorld;

		// Build chain from immediate parent up to root.
		TArray<const FSeinChildTransform*, TInlineAllocator<8>> Chain;
		const FSeinChildTransform* Cursor = FindByTagConst(Nodes, Leaf->ParentTag);
		int32 Depth = 0;
		while (Cursor && Depth < MaxParentChainDepthMutation)
		{
			Chain.Add(Cursor);
			if (!Cursor->ParentTag.IsValid()) break;
			Cursor = FindByTagConst(Nodes, Cursor->ParentTag);
			++Depth;
		}

		// Compose root-down: World = Entity * R * ... * ImmediateParent.
		FFixedTransform W = EntityWorld;
		for (int32 i = Chain.Num() - 1; i >= 0; --i)
		{
			W = W * Chain[i]->LocalTransform;
		}
		return W;
	}

	/** Wrap a yaw delta to (-π, π]. Inline to avoid a SeinARTSMovement
	 *  dependency from CoreEntity (Movement is a downstream module). */
	FFixedPoint WrapAngleDelta(FFixedPoint Delta)
	{
		while (Delta > FFixedPoint::Pi)  { Delta = Delta - FFixedPoint::TwoPi; }
		while (Delta < -FFixedPoint::Pi) { Delta = Delta + FFixedPoint::TwoPi; }
		return Delta;
	}
}

bool USeinSimMutationBPFL::SeinSetChildLocalRotation(const UObject* WCO, FSeinEntityHandle Handle, FGameplayTag Tag, FFixedQuaternion NewRotation)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetChildLocalRotation"))) return false;
	FSeinChildTransformsPayload* Data =
		S->GetComponentMutable<FSeinChildTransformsPayload>(
			Handle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetChildLocalRotation: entity %s has no FSeinChildTransformsPayload"), *Handle.ToString()); return false; }
	FSeinChildTransform* Found = FindByTagMutable(Data->Children, Tag);
	if (!Found) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetChildLocalRotation: entity %s has no child with tag %s"), *Handle.ToString(), *Tag.ToString()); return false; }
	Found->LocalTransform.Rotation = NewRotation;
	return true;
}

bool USeinSimMutationBPFL::SeinSetChildLocalTransform(const UObject* WCO, FSeinEntityHandle Handle, FGameplayTag Tag, FFixedTransform NewTransform)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("SetChildLocalTransform"))) return false;
	FSeinChildTransformsPayload* Data =
		S->GetComponentMutable<FSeinChildTransformsPayload>(
			Handle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetChildLocalTransform: entity %s has no FSeinChildTransformsPayload"), *Handle.ToString()); return false; }
	FSeinChildTransform* Found = FindByTagMutable(Data->Children, Tag);
	if (!Found) { UE_LOG(LogSeinBPFL, Warning, TEXT("SetChildLocalTransform: entity %s has no child with tag %s"), *Handle.ToString(), *Tag.ToString()); return false; }
	Found->LocalTransform = NewTransform;
	return true;
}

bool USeinSimMutationBPFL::SeinTurnChildToward(const UObject* WCO, FSeinEntityHandle Handle, FGameplayTag Tag,
	FFixedVector WorldTarget, FFixedPoint TurnRateRadPerSec, FFixedPoint DeltaTime)
{
	USeinWorldSubsystem* S = GetWorldSubsystem(WCO);
	if (!S) return false;
	if (!S->RequireStateMutationAuthorization(TEXT("TurnChildToward"))) return false;

	const FSeinEntity* Entity = S->GetEntity(Handle);
	if (!Entity) return false;

	FSeinChildTransformsPayload* Data =
		S->GetComponentMutable<FSeinChildTransformsPayload>(
			Handle);
	if (!Data) { UE_LOG(LogSeinBPFL, Warning, TEXT("TurnChildToward: entity %s has no FSeinChildTransformsPayload"), *Handle.ToString()); return false; }

	FSeinChildTransform* Found = FindByTagMutable(Data->Children, Tag);
	if (!Found) { UE_LOG(LogSeinBPFL, Warning, TEXT("TurnChildToward: entity %s has no child with tag %s"), *Handle.ToString(), *Tag.ToString()); return false; }

	// Compose the parent's world transform — needed both to derive the
	// child's CURRENT world transform AND to back-solve the new local
	// rotation from the desired world rotation.
	const FFixedTransform ParentWorld = ComposeParentWorld(Data->Children, Found, Entity->Transform);
	const FFixedTransform CurrentChildWorld = ParentWorld * Found->LocalTransform;

	// Yaw-only aim. For ground RTS (turret yaw, MG mount yaw) this is what
	// designers want; pitch/roll mount helpers can be added later.
	const FFixedVector CurForward = CurrentChildWorld.Rotation.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint CurYaw = SeinMath::Atan2(CurForward.Y, CurForward.X);

	// Vector from CHILD's world anchor toward the target — planar. If target
	// is on top of the child (zero-length), no aim change (keeps last frame's
	// facing — matches the convention used elsewhere in the framework for
	// degenerate aim cases).
	const FFixedPoint DX = WorldTarget.X - CurrentChildWorld.Location.X;
	const FFixedPoint DY = WorldTarget.Y - CurrentChildWorld.Location.Y;
	if (DX * DX + DY * DY <= FFixedPoint::Epsilon)
	{
		return true;
	}
	const FFixedPoint DesiredYaw = SeinMath::Atan2(DY, DX);

	// Shortest signed delta, clamp by TurnRate × dt — same shape used in
	// every Movement subclass for yaw-rate-limited rotation.
	FFixedPoint Delta = WrapAngleDelta(DesiredYaw - CurYaw);
	const FFixedPoint MaxStep = TurnRateRadPerSec * DeltaTime;
	if (Delta > MaxStep)  { Delta = MaxStep; }
	if (Delta < -MaxStep) { Delta = -MaxStep; }

	// Build the new world rotation as yaw-only, then back-solve into local
	// space: NewLocal.Rotation = ParentWorld.Rotation^-1 * NewWorld.Rotation.
	// Translation/scale on the local stay untouched — only rotation moves.
	const FFixedPoint NewWorldYaw = CurYaw + Delta;
	const FFixedQuaternion NewWorldRot = FFixedQuaternion::MakeFromEulers(
		FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, NewWorldYaw));
	const FFixedQuaternion ParentInv = ParentWorld.Rotation.Inverse();
	Found->LocalTransform.Rotation = ParentInv * NewWorldRot;
	return true;
}
