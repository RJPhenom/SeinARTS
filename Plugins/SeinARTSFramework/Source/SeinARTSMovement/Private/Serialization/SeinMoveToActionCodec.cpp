/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToActionCodec.cpp
 */

#include "Serialization/SeinMoveToActionCodec.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Actions/SeinMoveToAction.h"
#include "Components/SeinMovementPayload.h"
#include "Data/SeinWorldSnapshot.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinMovement.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinMoveToActionContinuation.h"
#include "Serialization/SeinMovementCanonicalState.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "UObject/FieldIterator.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SeinNavigationPayload.h"
#include "SeinNavigationSubsystem.h"
#endif

namespace
{
	constexpr int32 MaxPathWaypoints = 131072;
	constexpr int32 MaxPathSegments = 131072;
	constexpr int32 MaxRouteFunctionCharacters = 128;
	constexpr int32 MaxGeneratedMoveToNodes = 256;
	constexpr int32 MaxActionCounter = 1000000;

	const FName OwnerModuleId(TEXT("SeinARTSMovement"));
	constexpr TCHAR ContinuationDiagnosticToken[] =
		TEXT("[SEIN-MOVETO-CONTINUATION]");

	FSeinCanonicalStateKey MovementStateKey()
	{
		FSeinCanonicalStateKey Key;
		Key.StableDomainId = TEXT("seinarts.movement");
		Key.StableContributorId =
			TEXT("persistent-policy-instances");
		return Key;
	}

	struct FMoveToRouteSpec
	{
		FName Channel;
		FSeinMoveToDelegate USeinMoveToProxy::* Delegate = nullptr;
		FString FSeinMoveToActionContinuation::* Function = nullptr;
	};

	const FMoveToRouteSpec* RouteSpecs()
	{
		static const FMoveToRouteSpec Specs[] = {
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnCompleted),
				&USeinMoveToProxy::OnCompleted,
				&FSeinMoveToActionContinuation::
					OnCompletedFunction
			},
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnFailed),
				&USeinMoveToProxy::OnFailed,
				&FSeinMoveToActionContinuation::
					OnFailedFunction
			},
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnWaypointReached),
				&USeinMoveToProxy::OnWaypointReached,
				&FSeinMoveToActionContinuation::
					OnWaypointReachedFunction
			},
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnCancelled),
				&USeinMoveToProxy::OnCancelled,
				&FSeinMoveToActionContinuation::
					OnCancelledFunction
			},
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnPartialPath),
				&USeinMoveToProxy::OnPartialPath,
				&FSeinMoveToActionContinuation::
					OnPartialPathFunction
			},
			{
				GET_MEMBER_NAME_CHECKED(
					USeinMoveToProxy, OnPathRecomputed),
				&USeinMoveToProxy::OnPathRecomputed,
				&FSeinMoveToActionContinuation::
					OnPathRecomputedFunction
			},
		};
		return Specs;
	}

	constexpr int32 RouteSpecCount = 6;

	bool IsGeneratedLocalName(
		const FString& Name,
		const FString& BaseName)
	{
		if (Name == BaseName)
		{
			return true;
		}

		const FString Prefix = BaseName + TEXT("_");
		if (!Name.StartsWith(
				Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}
		const FString Suffix = Name.RightChop(Prefix.Len());
		if (Suffix.IsEmpty() || Suffix[0] == TEXT('0'))
		{
			return false;
		}
		for (const TCHAR Character : Suffix)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
		return true;
	}

	bool IsKismetIsValidResultName(const FString& Name)
	{
		return IsGeneratedLocalName(
			Name,
			TEXT("CallFunc_IsValid_ReturnValue"));
	}

	bool IsGeneratedDelegateResultName(const FString& Name)
	{
		return IsGeneratedLocalName(
			Name,
			TEXT("K2Node_CreateDelegate_OutputDelegate"));
	}

	bool IsGeneratedProxyResultName(const FString& Name)
	{
		return IsGeneratedLocalName(
			Name,
			TEXT("CallFunc_SeinMoveTo_ReturnValue"));
	}

	bool IsGeneratedAsyncResultStorageName(const FString& Name)
	{
		return IsGeneratedLocalName(
			Name,
			TEXT("Temp_struct_Variable"));
	}

	bool IsGeneratedAsyncCallbackResultName(const FString& Name)
	{
		return IsGeneratedLocalName(
			Name,
			TEXT("K2Node_CustomEvent_Result"));
	}

	const UFunction* DelegateSignature(const FMoveToRouteSpec& Spec)
	{
		const FMulticastDelegateProperty* Property =
			FindFProperty<FMulticastDelegateProperty>(
				USeinMoveToProxy::StaticClass(), Spec.Channel);
		return Property ? Property->SignatureFunction : nullptr;
	}

	bool ParseStandardGeneratedRouteName(
		const FString& FunctionName,
		FName Channel,
		FGuid& OutNodeGuid)
	{
		OutNodeGuid.Invalidate();
		if (FunctionName.IsEmpty()
			|| FunctionName.Len() > MaxRouteFunctionCharacters)
		{
			return false;
		}

		const FString Prefix = Channel.ToString() + TEXT("_");
		if (!FunctionName.StartsWith(
				Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}

		const FString GuidText =
			FunctionName.RightChop(Prefix.Len());
		return FGuid::ParseExact(
				GuidText, EGuidFormats::Digits, OutNodeGuid)
			&& OutNodeGuid.IsValid()
			&& OutNodeGuid.ToString(EGuidFormats::Digits)
				== GuidText;
	}

	bool IsStandardGeneratedRouteName(
		const FString& FunctionName,
		FName Channel)
	{
		FGuid Ignored;
		return ParseStandardGeneratedRouteName(
			FunctionName, Channel, Ignored);
	}

	bool IsExactGeneratedRouteFunction(
		const UClass& AbilityClass,
		const UFunction& Function,
		const FMoveToRouteSpec& Spec)
	{
		const UFunction* Signature = DelegateSignature(Spec);
		const UClass* FunctionClass = Function.GetOuterUClass();
		return Signature
			&& IsStandardGeneratedRouteName(
				Function.GetName(), Spec.Channel)
			&& Cast<UBlueprintGeneratedClass>(FunctionClass)
			&& AbilityClass.IsChildOf(FunctionClass)
			&& Signature->IsSignatureCompatibleWith(&Function)
			&& Function.IsSignatureCompatibleWith(Signature);
	}

	const FMoveToRouteSpec* FindExactGeneratedRouteSpec(
		const USeinAbility& Ability,
		const FDelegateProperty& Property,
		const FScriptDelegate& Binding,
		FGuid& OutNodeGuid)
	{
		OutNodeGuid.Invalidate();
		if (Binding.GetUObjectEvenIfUnreachable() != &Ability)
		{
			return nullptr;
		}

		const FString FunctionName =
			Binding.GetFunctionName().ToString();
		const UFunction* Function =
			Ability.GetClass()->FindFunctionByName(
				Binding.GetFunctionName());
		if (!Function
			|| Function->GetName() != FunctionName)
		{
			return nullptr;
		}

		for (int32 Index = 0; Index < RouteSpecCount; ++Index)
		{
			const FMoveToRouteSpec& Spec = RouteSpecs()[Index];
			FGuid NodeGuid;
			if (Property.SignatureFunction
					== DelegateSignature(Spec)
				&& ParseStandardGeneratedRouteName(
					FunctionName, Spec.Channel, NodeGuid)
				&& IsExactGeneratedRouteFunction(
					*Ability.GetClass(), *Function, Spec))
			{
				OutNodeGuid = NodeGuid;
				return &Spec;
			}
		}
		return nullptr;
	}

	int32 RouteSpecIndex(const FMoveToRouteSpec& Spec)
	{
		for (int32 Index = 0; Index < RouteSpecCount; ++Index)
		{
			if (&RouteSpecs()[Index] == &Spec)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool ResolveCurrentRouteIdentity(
		const USeinAbility& Ability,
		const FSeinMoveToActionContinuation& State,
		const UBlueprintGeneratedClass*& OutFrameClass,
		TOptional<FGuid>& OutNodeGuid,
		FString& OutError)
	{
		OutFrameClass = nullptr;
		OutNodeGuid.Reset();
		for (int32 Index = 0; Index < RouteSpecCount; ++Index)
		{
			const FMoveToRouteSpec& Spec = RouteSpecs()[Index];
			const FString& FunctionName =
				State.*(Spec.Function);
			if (FunctionName.IsEmpty())
			{
				continue;
			}

			const UFunction* Function =
				Ability.GetClass()->FindFunctionByName(
					FName(*FunctionName));
			const UBlueprintGeneratedClass* FunctionClass =
				Function
					? Cast<UBlueprintGeneratedClass>(
						Function->GetOuterUClass())
					: nullptr;
			FGuid NodeGuid;
			if (!Function
				|| !FunctionClass
				|| !ParseStandardGeneratedRouteName(
					FunctionName, Spec.Channel, NodeGuid)
				|| !IsExactGeneratedRouteFunction(
					*Ability.GetClass(), *Function, Spec))
			{
				OutError = FString::Printf(
					TEXT("%s Move To route '%s' is not an exact generated async-node callback."),
					ContinuationDiagnosticToken,
					*Spec.Channel.ToString());
				return false;
			}
			if (OutNodeGuid.IsSet()
				&& (OutNodeGuid.GetValue() != NodeGuid
					|| OutFrameClass != FunctionClass))
			{
				OutError = FString::Printf(
					TEXT("%s Move To routes do not identify one generated async node in one Blueprint frame."),
					ContinuationDiagnosticToken);
				return false;
			}
			OutFrameClass = FunctionClass;
			OutNodeGuid = NodeGuid;
		}
		return true;
	}

	bool ValidateBlueprintFrameResidue(
		const FSeinMoveToActionContinuation& State,
		const USeinAbility& Ability,
		const USeinMoveToProxy& Proxy,
		FString& OutError)
	{
		constexpr uint8 CompleteRouteMask =
			(1u << RouteSpecCount) - 1u;
		const UBlueprintGeneratedClass* CurrentFrameClass =
			nullptr;
		TOptional<FGuid> CurrentNodeGuid;
		if (!ResolveCurrentRouteIdentity(
				Ability,
				State,
				CurrentFrameClass,
				CurrentNodeGuid,
				OutError))
		{
			return false;
		}

		for (const UClass* Class = Ability.GetClass();
			Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* FrameClass =
				Cast<UBlueprintGeneratedClass>(Class);
			UFunction* Function = FrameClass
				? FrameClass->UberGraphFunction
				: nullptr;
			if (!FrameClass || !Function)
			{
				continue;
			}

			uint8* LiveFrame =
				FrameClass->GetPersistentUberGraphFrame(
					const_cast<USeinAbility*>(&Ability),
					Function);
			FStructOnScope DefaultFrame(Function);
			if (!LiveFrame || !DefaultFrame.IsValid())
			{
				OutError = FString::Printf(
					TEXT("%s Move To could not inspect Blueprint persistent frame '%s'."),
					ContinuationDiagnosticToken,
					*FrameClass->GetPathName());
				return false;
			}

			TMap<FGuid, uint8> RouteMasks;
			TArray<const USeinMoveToProxy*,
				TInlineAllocator<8>> ProxyResults;
			int32 TrueValidityResults = 0;
			int32 GeneratedResultStorageCount = 0;
			int32 GeneratedCallbackResultCount = 0;
			for (FProperty* Property = Function->PropertyLink;
				Property;
				Property = Property->PropertyLinkNext)
			{
				const FStructProperty* StructProperty =
					CastField<FStructProperty>(Property);
				if (!StructProperty
					|| Property->HasAnyPropertyFlags(CPF_Parm)
					|| Property->ArrayDim != 1
					|| StructProperty->Struct
						!= FSeinMoveToResult::StaticStruct())
				{
					continue;
				}
				if (IsGeneratedAsyncResultStorageName(
						Property->GetName()))
				{
					++GeneratedResultStorageCount;
				}
				else if (IsGeneratedAsyncCallbackResultName(
						Property->GetName()))
				{
					++GeneratedCallbackResultCount;
				}
			}
			const bool bHasCertifiedGeneratedResultShape =
				GeneratedResultStorageCount > 0
				&& GeneratedResultStorageCount
					<= MaxGeneratedMoveToNodes
				&& GeneratedCallbackResultCount
					== GeneratedResultStorageCount
						* RouteSpecCount;
			bool bSawGeneratedResultResidue = false;

			for (FProperty* Property = Function->PropertyLink;
				Property;
				Property = Property->PropertyLinkNext)
			{
				if (Property->HasAnyPropertyFlags(CPF_Parm))
				{
					continue;
				}
				for (int32 ArrayIndex = 0;
					ArrayIndex < Property->ArrayDim;
					++ArrayIndex)
				{
					if (Property->Identical_InContainer(
						LiveFrame,
						DefaultFrame.GetStructMemory(),
						ArrayIndex))
					{
						continue;
					}

					bool bKnownDeadResidue = false;
					if (const FDelegateProperty* DelegateProperty =
							CastField<FDelegateProperty>(Property))
					{
						const FScriptDelegate DefaultValue =
							DelegateProperty
								->GetPropertyValue_InContainer(
									DefaultFrame.GetStructMemory(),
									ArrayIndex);
						const FScriptDelegate LiveValue =
							DelegateProperty
								->GetPropertyValue_InContainer(
									LiveFrame, ArrayIndex);
						FGuid NodeGuid;
						const FMoveToRouteSpec* Spec =
							Property->ArrayDim == 1
								&& ArrayIndex == 0
								&& IsGeneratedDelegateResultName(
									Property->GetName())
								&& !DefaultValue
									.GetUObjectEvenIfUnreachable()
								&& DefaultValue.GetFunctionName()
									.IsNone()
							? FindExactGeneratedRouteSpec(
								Ability, *DelegateProperty,
								LiveValue, NodeGuid)
							: nullptr;
						const int32 SpecIndex = Spec
							? RouteSpecIndex(*Spec)
							: INDEX_NONE;
						const UFunction* RouteFunction = Spec
							? Ability.GetClass()
								->FindFunctionByName(
									LiveValue.GetFunctionName())
							: nullptr;
						if (SpecIndex != INDEX_NONE
							&& RouteFunction
							&& RouteFunction->GetOuterUClass()
								== FrameClass)
						{
							uint8& Mask =
								RouteMasks.FindOrAdd(NodeGuid);
							const uint8 RouteBit =
								1u << SpecIndex;
							if ((Mask & RouteBit) != 0)
							{
								OutError = FString::Printf(
									TEXT("%s Move To Blueprint frame contains a duplicate generated route residue."),
									ContinuationDiagnosticToken);
								return false;
							}
							Mask |= RouteBit;
							bKnownDeadResidue = true;
						}
					}
					else if (const FObjectPropertyBase*
						ObjectProperty = CastField<
							FObjectPropertyBase>(Property))
					{
						const UObject* DefaultValue =
							ObjectProperty
								->GetObjectPropertyValue_InContainer(
									DefaultFrame.GetStructMemory(),
									ArrayIndex);
						const UObject* LiveValue =
							ObjectProperty
								->GetObjectPropertyValue_InContainer(
									LiveFrame, ArrayIndex);
						const USeinMoveToProxy* MoveProxy =
							Cast<USeinMoveToProxy>(LiveValue);
						if (Property->ArrayDim == 1
							&& ArrayIndex == 0
							&& IsGeneratedProxyResultName(
								Property->GetName())
							&& ObjectProperty->PropertyClass
								== USeinMoveToProxy::StaticClass()
							&& !DefaultValue
							&& MoveProxy
							&& MoveProxy->GetClass()
								== USeinMoveToProxy::StaticClass())
						{
							ProxyResults.Add(MoveProxy);
							bKnownDeadResidue = true;
						}
					}
					else if (const FBoolProperty* BoolProperty =
							CastField<FBoolProperty>(Property);
						BoolProperty
						&& Property->ArrayDim == 1
						&& ArrayIndex == 0
						&& IsKismetIsValidResultName(
							Property->GetName())
						&& !BoolProperty
							->GetPropertyValue_InContainer(
								DefaultFrame.GetStructMemory())
						&& BoolProperty
							->GetPropertyValue_InContainer(
								LiveFrame))
					{
						++TrueValidityResults;
						bKnownDeadResidue = true;
					}
					else if (const FStructProperty* StructProperty =
							CastField<FStructProperty>(Property);
						StructProperty
						&& Property->ArrayDim == 1
						&& ArrayIndex == 0
						&& StructProperty->Struct
							== FSeinMoveToResult::StaticStruct()
						&& bHasCertifiedGeneratedResultShape
						&& (IsGeneratedAsyncResultStorageName(
								Property->GetName())
							|| IsGeneratedAsyncCallbackResultName(
								Property->GetName())))
					{
						// UE 5.7 BaseAsyncTask expansion emits one internal
						// Result store and one custom-event Result parameter
						// per delegate. Accept these values only when the
						// complete generated name/type/count topology is
						// present. Compile, data-validation, PIE, and cook
						// gates separately prove that every Result use is
						// synchronous or promoted to canonical state.
						bSawGeneratedResultResidue = true;
						bKnownDeadResidue = true;
					}

					if (!bKnownDeadResidue)
					{
						OutError = FString::Printf(
							TEXT("%s Move To cannot checkpoint non-default Blueprint frame value '%s.%s[%d]'; persist future-needed values in deterministic ability member/component state, then read or recompute them after the Move To callback."),
							ContinuationDiagnosticToken,
							*FrameClass->GetPathName(),
							*Property->GetName(),
							ArrayIndex);
						return false;
					}
				}
			}

			if (RouteMasks.Num() > MaxGeneratedMoveToNodes
				|| ProxyResults.Num() != RouteMasks.Num()
				|| TrueValidityResults != RouteMasks.Num()
				|| (bSawGeneratedResultResidue
					&& (!bHasCertifiedGeneratedResultShape
						|| RouteMasks.Num()
							> GeneratedResultStorageCount)))
			{
				OutError = FString::Printf(
					TEXT("%s Move To Blueprint frame has an incomplete generated async-node residue group."),
					ContinuationDiagnosticToken);
				return false;
			}
			for (const TPair<FGuid, uint8>& Pair : RouteMasks)
			{
				if (Pair.Value != CompleteRouteMask)
				{
					OutError = FString::Printf(
						TEXT("%s Move To Blueprint frame has an incomplete generated route residue group."),
						ContinuationDiagnosticToken);
					return false;
				}
			}
			TSet<const USeinMoveToProxy*> UniqueProxies;
			for (const USeinMoveToProxy* MoveProxy :
				ProxyResults)
			{
				UniqueProxies.Add(MoveProxy);
			}
			if (UniqueProxies.Num() != ProxyResults.Num())
			{
				OutError = FString::Printf(
					TEXT("%s Move To Blueprint frame reuses one proxy result across generated nodes."),
					ContinuationDiagnosticToken);
				return false;
			}

			if (CurrentNodeGuid.IsSet()
				&& CurrentFrameClass == FrameClass
				&& RouteMasks.Contains(
					CurrentNodeGuid.GetValue())
				&& !ProxyResults.Contains(&Proxy))
			{
				OutError = FString::Printf(
					TEXT("%s Move To Blueprint frame does not retain the active proxy for its generated node."),
					ContinuationDiagnosticToken);
				return false;
			}
		}
		return true;
	}

	bool CaptureRoute(
		const USeinAbility& Ability,
		const FSeinMoveToDelegate& Delegate,
		const FMoveToRouteSpec& Spec,
		FString& OutFunction,
		FString& OutError)
	{
		OutFunction.Reset();
		if (!Delegate.IsBound())
		{
			return true;
		}

		const TArray<UObject*> Targets =
			Delegate.GetAllObjectsEvenIfUnreachable();
		if (Targets.Num() != 1 || Targets[0] != &Ability)
		{
			OutError = FString::Printf(
				TEXT("Move To route '%s' must have exactly one binding to its owning ability."),
				*Spec.Channel.ToString());
			return false;
		}

		TArray<const UFunction*, TInlineAllocator<2>>
			MatchingFunctions;
		for (TFieldIterator<UFunction> It(
			Ability.GetClass(),
			EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const UFunction* Function = *It;
			if (Function
				&& IsExactGeneratedRouteFunction(
					*Ability.GetClass(), *Function, Spec)
				&& Delegate.Contains(
					&Ability, Function->GetFName()))
			{
				MatchingFunctions.Add(Function);
			}
		}
		if (MatchingFunctions.Num() != 1)
		{
			OutError = FString::Printf(
				TEXT("Move To route '%s' is not one unambiguous standard async-node callback."),
				*Spec.Channel.ToString());
			return false;
		}

		OutFunction = MatchingFunctions[0]->GetName();
		return true;
	}

	bool ValidateStagedRoute(
		const USeinAbility& Ability,
		const FMoveToRouteSpec& Spec,
		const FString& FunctionName,
		FString& OutError)
	{
		if (FunctionName.IsEmpty())
		{
			return true;
		}
		if (!IsStandardGeneratedRouteName(
			FunctionName, Spec.Channel))
		{
			OutError = FString::Printf(
				TEXT("Move To route '%s' has a nonstandard generated function name."),
				*Spec.Channel.ToString());
			return false;
		}

		const UFunction* Function =
			Ability.GetClass()->FindFunctionByName(
				FName(*FunctionName));
		if (!Function
			|| Function->GetName() != FunctionName
			|| !IsExactGeneratedRouteFunction(
				*Ability.GetClass(), *Function, Spec))
		{
			OutError = FString::Printf(
				TEXT("Move To route '%s' does not resolve to its exact generated callback/signature on ability class '%s'."),
				*Spec.Channel.ToString(),
				*Ability.GetClass()->GetPathName());
			return false;
		}
		return true;
	}

	bool IsNonNegative(FFixedPoint Value)
	{
		return Value >= FFixedPoint::Zero;
	}

	bool ValidateContinuationShape(
		const FSeinMoveToActionContinuation& State,
		FString& OutError)
	{
		if (State.Path.Waypoints.Num() > MaxPathWaypoints
			|| State.Path.Segments.Num() > MaxPathSegments)
		{
			OutError =
				TEXT("Move To continuation path exceeds its element bound.");
			return false;
		}
		if (static_cast<uint8>(State.StuckPhase)
			> static_cast<uint8>(ESeinMoveStuckPhase::Escaping))
		{
			OutError =
				TEXT("Move To continuation stuck phase is outside its enum.");
			return false;
		}
		// While Escaping the harness drives the two-point escape leg, so the
		// cursor is bounded by that leg rather than by the (empty) order path.
		const bool bEscaping =
			State.StuckPhase == ESeinMoveStuckPhase::Escaping;
		if (State.CurrentWaypointIndex < 0
			|| (bEscaping && State.CurrentWaypointIndex > 1)
			|| (!bEscaping
				&& !State.Path.Waypoints.IsEmpty()
				&& !State.Path.Waypoints.IsValidIndex(
					State.CurrentWaypointIndex))
			|| (!bEscaping
				&& State.Path.Waypoints.IsEmpty()
				&& State.CurrentWaypointIndex != 0))
		{
			OutError =
				TEXT("Move To continuation has an invalid waypoint cursor.");
			return false;
		}
		if (State.bPathResolved
			&& (!State.bHasMovementBinding
				|| (!bEscaping
					&& (!State.Path.bIsValid
						|| State.Path.Waypoints.IsEmpty()))))
		{
			OutError =
				TEXT("Resolved Move To continuation requires a valid path and persistent movement binding.");
			return false;
		}
		// The order path is discarded at escape entry and re-planned on exit,
		// so an Escaping record is resolved but carries no path of its own.
		if (bEscaping
			&& (!State.bPathResolved
				|| State.Path.bIsValid
				|| !State.Path.Waypoints.IsEmpty()
				|| !State.Path.Segments.IsEmpty()))
		{
			OutError =
				TEXT("Move To escape state requires a resolved order whose path was discarded at escape entry.");
			return false;
		}
		// Free is only ever entered together with a zeroed episode, and the
		// episode fields only mutate while non-Free.
		if (State.StuckPhase == ESeinMoveStuckPhase::Free
			&& (State.HoldTime != FFixedPoint::Zero
				|| State.HoldBoundariesFired != 0
				|| State.EscapeAttempts != 0))
		{
			OutError =
				TEXT("Move To continuation is Free but carries a live hold episode.");
			return false;
		}
		if (State.bMovementFinalized)
		{
			OutError =
				TEXT("An active Move To continuation cannot already be movement-finalized.");
			return false;
		}
		if (!IsNonNegative(State.AcceptanceRadius)
			|| !IsNonNegative(State.TimeSinceLastRepath)
			|| !IsNonNegative(State.BestDistToFinal)
			|| !IsNonNegative(State.TimeStalledNearGoal)
			|| !IsNonNegative(State.HoldTime)
			|| !IsNonNegative(State.EscapeAcceptanceRadius)
			|| !IsNonNegative(State.EscapeHoldTime)
			|| !IsNonNegative(State.FootprintRadius)
			|| !IsNonNegative(State.StallBand)
			|| !IsNonNegative(State.Path.TotalCost))
		{
			OutError =
				TEXT("Move To continuation contains a negative radius, cost, or clock.");
			return false;
		}
		// HoldBoundariesFired keeps growing through a policy-zero hold (pivot /
		// yield), so it needs a generous bound rather than none: the shared
		// defensive cap is ~3.5 days of continuously held time in one order, far
		// beyond any legitimate episode, and it keeps a tampered payload from
		// driving the boundary arithmetic anywhere near int32 range.
		if (State.ConsecutiveRepathFailures < 0
			|| State.ConsecutiveRepathFailures > MaxActionCounter
			|| State.EscapeAttempts < 0
			|| State.EscapeAttempts > MaxActionCounter
			|| State.TotalEscapeEntries < 0
			|| State.TotalEscapeEntries > MaxActionCounter
			|| State.HoldBoundariesFired < 0
			|| State.HoldBoundariesFired > MaxActionCounter)
		{
			OutError =
				TEXT("Move To continuation counter is outside its defensive bound.");
			return false;
		}
		for (const FSeinPathSegment& Segment :
			State.Path.Segments)
		{
			if (static_cast<uint8>(Segment.Type)
					> static_cast<uint8>(
						ESeinPathSegmentType::Jump)
				|| !IsNonNegative(Segment.Radius))
			{
				OutError =
					TEXT("Move To continuation contains an invalid typed path segment.");
				return false;
			}
		}
		return true;
	}

	const FSeinMovementPolicyInstanceState*
	FindMovementRecord(
		const FSeinMovementCanonicalState& State,
		FSeinEntityHandle Entity)
	{
		int32 First = 0;
		int32 Last = State.MovementInstances.Num();
		while (First < Last)
		{
			const int32 Middle = First + (Last - First) / 2;
			if (State.MovementInstances[Middle].Entity < Entity)
			{
				First = Middle + 1;
			}
			else
			{
				Last = Middle;
			}
		}
		return State.MovementInstances.IsValidIndex(First)
				&& State.MovementInstances[First].Entity == Entity
			? &State.MovementInstances[First]
			: nullptr;
	}

	FString AuthoredMovementClassPath(
		const FSeinMovementPayload& Component)
	{
		return Component.MovementClass.IsNull()
			? USeinBasicMovement::StaticClass()->GetPathName()
			: Component.MovementClass.ToString();
	}

	struct FMoveToRestoreStage final
		: ISeinLatentActionRestoreStage
	{
		FSeinMoveToActionContinuation State;
		FString MovementClassPath;
	};
}

/**
 * The sole private-field mapping for USeinMoveToAction. Keeping capture and
 * apply adjacent makes additions mechanically reviewable and round-trip
 * testable; codec lambdas never hand-map action state.
 */
struct FSeinMoveToActionCodec
{
	static void ReadActionState(
		const USeinMoveToAction& Action,
		FSeinMoveToActionContinuation& State)
	{
		State.Destination = Action.Destination;
		State.AcceptanceRadius = Action.AcceptanceRadius;
		State.CurrentWaypointIndex =
			Action.CurrentWaypointIndex;
		State.bPathResolved = Action.bPathResolved;
		State.bAuthoritativeDestination =
			Action.bAuthoritativeDestination;
		State.PathOriginAgentPos =
			Action.PathOriginAgentPos;
		State.TimeSinceLastRepath =
			Action.TimeSinceLastRepath;
		State.ConsecutiveRepathFailures =
			Action.ConsecutiveRepathFailures;
		State.BestDistToFinal = Action.BestDistToFinal;
		State.TimeStalledNearGoal =
			Action.TimeStalledNearGoal;
		State.StuckPhase = Action.StuckPhase;
		State.HoldTime = Action.HoldTime;
		State.HoldBoundariesFired =
			Action.HoldBoundariesFired;
		State.bForceRepathNow =
			Action.bForceRepathNow;
		State.EscapeOrigin = Action.EscapeOrigin;
		State.EscapeTarget = Action.EscapeTarget;
		State.EscapeAcceptanceRadius =
			Action.EscapeAcceptanceRadius;
		State.EscapeHoldTime = Action.EscapeHoldTime;
		State.EscapeAttempts = Action.EscapeAttempts;
		State.TotalEscapeEntries =
			Action.TotalEscapeEntries;
		State.FootprintRadius = Action.FootprintRadius;
		State.StallBand = Action.StallBand;
		// Copy only canonical path fields. DebugCellPath may be very large and
		// is diagnostic-only, so even a temporary payload-side allocation
		// would be both misleading and wasteful.
		State.Path.Waypoints = Action.Path.Waypoints;
		State.Path.Segments = Action.Path.Segments;
		State.Path.TotalCost = Action.Path.TotalCost;
		State.Path.bIsValid = Action.Path.bIsValid;
		State.Path.bIsPartial = Action.Path.bIsPartial;
		State.bHasMovementBinding =
			Action.Movement != nullptr;
		State.bMovementFinalized =
			Action.bMovementFinalized;
	}

	static void ApplyActionState(
		const FSeinMoveToActionContinuation& State,
		USeinMoveToAction& Action)
	{
		Action.Destination = State.Destination;
		Action.AcceptanceRadius = State.AcceptanceRadius;
		Action.CurrentWaypointIndex =
			State.CurrentWaypointIndex;
		Action.bPathResolved = State.bPathResolved;
		Action.bAuthoritativeDestination =
			State.bAuthoritativeDestination;
		Action.PathOriginAgentPos =
			State.PathOriginAgentPos;
		Action.TimeSinceLastRepath =
			State.TimeSinceLastRepath;
		Action.ConsecutiveRepathFailures =
			State.ConsecutiveRepathFailures;
		Action.BestDistToFinal = State.BestDistToFinal;
		Action.TimeStalledNearGoal =
			State.TimeStalledNearGoal;
		Action.StuckPhase = State.StuckPhase;
		Action.HoldTime = State.HoldTime;
		Action.HoldBoundariesFired =
			State.HoldBoundariesFired;
		Action.bForceRepathNow =
			State.bForceRepathNow;
		Action.EscapeOrigin = State.EscapeOrigin;
		Action.EscapeTarget = State.EscapeTarget;
		Action.EscapeAcceptanceRadius =
			State.EscapeAcceptanceRadius;
		Action.EscapeHoldTime = State.EscapeHoldTime;
		Action.EscapeAttempts = State.EscapeAttempts;
		Action.TotalEscapeEntries =
			State.TotalEscapeEntries;
		Action.FootprintRadius = State.FootprintRadius;
		Action.StallBand = State.StallBand;
		Action.Path = State.Path;
		Action.bMovementFinalized =
			State.bMovementFinalized;
		// InitialThrottleStreak is diagnostic-only and deliberately starts
		// fresh after restore. DebugCellPath was absent from the wire.
		Action.InitialThrottleStreak = 0;
		Action.Path.DebugCellPath.Reset();
	}

	static bool Capture(
		const FSeinLatentActionCaptureContext& Context,
		FInstancedStruct& OutPayload,
		FString& OutError)
	{
		OutPayload.Reset();
		const USeinMoveToAction* Action =
			Cast<USeinMoveToAction>(&Context.Action);
		const USeinAbility* Ability =
			Action ? Action->OwningAbility.Get() : nullptr;
		USeinMoveToProxy* Proxy =
			Action ? Action->Observer.Get() : nullptr;
		if (!Action
			|| Action->GetClass()
				!= USeinMoveToAction::StaticClass()
			|| !Ability
			|| Ability->OwnerEntity != Action->OwnerEntity
			|| !Ability->bIsActive
			|| Ability->GetActivationID()
				!= Context.AbilityActivationId
			|| !Proxy
			|| Proxy->GetClass()
				!= USeinMoveToProxy::StaticClass()
			|| Action->GetOuter() != Proxy
			|| Proxy->RunningAction != Action
			|| Proxy->CachedAbility != Ability
			|| Proxy->CachedDestination
				!= Action->Destination)
		{
			OutError =
				TEXT("Move To capture requires the exact active action/proxy/ability ownership graph.");
			return false;
		}

		FSeinMoveToActionContinuation State;
		ReadActionState(*Action, State);
		for (int32 Index = 0;
			Index < RouteSpecCount; ++Index)
		{
			const FMoveToRouteSpec& Spec =
				RouteSpecs()[Index];
			if (!CaptureRoute(
				*Ability,
				Proxy->*(Spec.Delegate),
				Spec,
				State.*(Spec.Function),
				OutError))
			{
				return false;
			}
		}
		if (!ValidateBlueprintFrameResidue(
			State, *Ability, *Proxy, OutError))
		{
			return false;
		}
		if (!ValidateContinuationShape(State, OutError))
		{
			return false;
		}

		if (State.bHasMovementBinding)
		{
			const UWorld* UnrealWorld =
				Context.World.GetWorld();
			const USeinMovementSubsystem* MovementSubsystem =
				UnrealWorld
					? UnrealWorld->GetSubsystem<
						USeinMovementSubsystem>()
					: nullptr;
			if (!MovementSubsystem
				|| MovementSubsystem->FindMovementInstance(
					Action->OwnerEntity) != Action->Movement)
			{
				OutError =
					TEXT("Move To action does not borrow the owner's canonical persistent movement instance.");
				return false;
			}
		}

		OutPayload =
			FInstancedStruct::Make(MoveTemp(State));
		return true;
	}

	static bool StageRestore(
		const FSeinLatentActionStageContext& Context,
		const FInstancedStruct& Payload,
		TUniquePtr<ISeinLatentActionRestoreStage>& OutStage,
		FString& OutError)
	{
		OutStage.Reset();
		const FSeinMoveToActionContinuation* State =
			Payload.GetPtr<
				FSeinMoveToActionContinuation>();
		const FSeinSnapshotLatentActionRecord* Record =
			Context.Record;
		const USeinAbility* Ability =
			Context.Candidate && Record
				? Context.Candidate->FindAbility(
					Record->AbilityPoolID)
				: nullptr;
		if (!State || !Record || !Context.Candidate
			|| !Context.Dependencies || !Ability
			|| !Ability->bIsActive
			|| Ability->OwnerEntity != Record->OwnerEntity
			|| Ability->GetActivationID()
				!= Record->AbilityActivationID)
		{
			OutError =
				TEXT("Move To restore requires its exact staged active ability and dependency views.");
			return false;
		}
		if (!ValidateContinuationShape(*State, OutError))
		{
			return false;
		}

		for (int32 Index = 0;
			Index < RouteSpecCount; ++Index)
		{
			const FMoveToRouteSpec& Spec =
				RouteSpecs()[Index];
			if (!ValidateStagedRoute(
				*Ability, Spec,
				State->*(Spec.Function), OutError))
			{
				return false;
			}
		}
		const UBlueprintGeneratedClass* RouteFrameClass =
			nullptr;
		TOptional<FGuid> RouteNodeGuid;
		if (!ResolveCurrentRouteIdentity(
				*Ability,
				*State,
				RouteFrameClass,
				RouteNodeGuid,
				OutError))
		{
			return false;
		}
		TUniquePtr<FMoveToRestoreStage> Stage =
			MakeUnique<FMoveToRestoreStage>();
		Stage->State = *State;
		if (State->bHasMovementBinding)
		{
			const FInstancedStruct* MovementPayload =
				Context.Dependencies->FindStagedPayload(
					MovementStateKey());
			const FSeinMovementCanonicalState* MovementState =
				MovementPayload
					? MovementPayload->GetPtr<
						FSeinMovementCanonicalState>()
					: nullptr;
			const FSeinMovementPolicyInstanceState*
				MovementRecord = MovementState
					? FindMovementRecord(
						*MovementState,
						Record->OwnerEntity)
					: nullptr;
			const FSeinMovementPayload* Component =
				Context.Candidate->FindComponent<
					FSeinMovementPayload>(
						Record->OwnerEntity);
			if (!MovementRecord || !Component)
			{
				OutError =
					TEXT("Move To continuation has no exact staged persistent movement instance for its owner.");
				return false;
			}
			Stage->MovementClassPath =
				AuthoredMovementClassPath(*Component);
			if (Stage->MovementClassPath.IsEmpty()
				|| MovementRecord->Object.ExactClassPath
					!= Stage->MovementClassPath)
			{
				OutError =
					TEXT("Move To continuation's staged movement class disagrees with its owner component.");
				return false;
			}
		}

		OutStage = MoveTemp(Stage);
		return true;
	}

	static USeinLatentAction* CommitRestore(
		FSeinLatentActionCommitContext& Context,
		TUniquePtr<ISeinLatentActionRestoreStage>&&
			OpaqueStage)
	{
		FMoveToRestoreStage* Stage =
			static_cast<FMoveToRestoreStage*>(
				OpaqueStage.Get());
		check(Stage);

		USeinMoveToProxy* Proxy =
			NewObject<USeinMoveToProxy>(&Context.World);
		check(Proxy);
		// Restored proxies are not born from K2 factory bytecode, so make
		// their lifetime explicit across arbitrary GC until terminal output
		// or silent timeline abandonment calls SetReadyToDestroy.
		Proxy->RegisterWithGameInstance(&Context.World);

		USeinMoveToAction* Action =
			NewObject<USeinMoveToAction>(Proxy);
		check(Action);
		ApplyActionState(Stage->State, *Action);
		Action->OwningAbility = &Context.OwningAbility;
		Action->OwnerEntity =
			Context.OwningAbility.OwnerEntity;
		Action->Observer = Proxy;

		if (Stage->State.bHasMovementBinding)
		{
			UWorld* UnrealWorld = Context.World.GetWorld();
			USeinMovementSubsystem* MovementSubsystem =
				UnrealWorld
					? UnrealWorld->GetSubsystem<
						USeinMovementSubsystem>()
					: nullptr;
			Action->Movement = MovementSubsystem
				? MovementSubsystem->FindMovementInstance(
					Action->OwnerEntity)
				: nullptr;
			check(Action->Movement);
			check(Action->Movement->GetClass()->GetPathName()
				== Stage->MovementClassPath);
		}
		else
		{
			Action->Movement = nullptr;
		}

		Proxy->CachedAbility = &Context.OwningAbility;
		Proxy->CachedDestination =
			Stage->State.Destination;
		Proxy->RunningAction = Action;
		for (int32 Index = 0;
			Index < RouteSpecCount; ++Index)
		{
			const FMoveToRouteSpec& Spec =
				RouteSpecs()[Index];
			const FString& FunctionName =
				Stage->State.*(Spec.Function);
			if (FunctionName.IsEmpty())
			{
				continue;
			}
			FScriptDelegate Binding;
			Binding.BindUFunction(
				&Context.OwningAbility,
				FName(*FunctionName));
			(Proxy->*(Spec.Delegate)).Add(Binding);
		}
		return Action;
	}
};

FSeinLatentActionCodecRegistrationHandle
SeinRegisterMoveToActionCodec(FString& OutError)
{
	OutError.Reset();

	FSeinLatentActionCodecDescriptor Descriptor;
	Descriptor.SupportedClass =
		USeinMoveToAction::StaticClass();
	Descriptor.StableCodecId =
		TEXT("seinarts.movement.move-to");
	// Schema 5 / codec 6 (2026-09-04): explicit ESeinMoveStuckPhase, integer
	// hold-boundary counter, and EscapeOrigin replace the stage-1 / escape-mode
	// booleans and the running NextEscalationAt clock; the escape leg no longer
	// rides in Path. Behavior revision unchanged: the ladder is bit-identical.
	Descriptor.StateSchemaVersion = 5;
	Descriptor.BehaviorRevision = 5;
	Descriptor.CodecRevision = 6;
	Descriptor.PayloadStruct =
		FSeinMoveToActionContinuation::StaticStruct();
	if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
		Descriptor.PayloadStruct,
		Descriptor.PayloadSchemaDigest,
		OutError))
	{
		return {};
	}
	Descriptor.Limits.MaxRecursionDepth = 32;
	Descriptor.Limits.MaxEncodedBytes =
		FSeinLatentActionCodecRegistry::MaxPayloadBytes;
	Descriptor.Limits.MaxAggregateElements =
		1024 * 1024;
	Descriptor.RequiredNativeContributors = {
		MovementStateKey()
	};

	FSeinLatentActionCodecOps Ops;
	Ops.Capture = &FSeinMoveToActionCodec::Capture;
	Ops.StageRestore =
		&FSeinMoveToActionCodec::StageRestore;
	Ops.CommitRestore =
		&FSeinMoveToActionCodec::CommitRestore;
	return FSeinLatentActionCodecRegistry::Register(
		OwnerModuleId, Descriptor, MoveTemp(Ops),
		&OutError);
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FSeinStructWireLimits MoveToTestWireLimits()
	{
		FSeinStructWireLimits Limits;
		Limits.MaxBytes =
			FSeinLatentActionCodecRegistry::MaxPayloadBytes;
		Limits.MaxAggregateElements = 1024 * 1024;
		Limits.MaxStringBytes = 1024 * 1024;
		Limits.MaxRecursionDepth = 32;
		Limits.MaxNativeAllocationBytes = Limits.MaxBytes;
		return Limits;
	}

	bool EncodeTestState(
		const FSeinMoveToActionContinuation& State,
		TArray<uint8>& OutBytes,
		FString& OutError)
	{
		const TArray<const UScriptStruct*> DynamicStructs;
		const TArray<FName> Names;
		return FSeinCanonicalStateCodec::Encode(
			FSeinMoveToActionContinuation::StaticStruct(),
			&State,
			{ DynamicStructs, Names },
			MoveToTestWireLimits(),
			OutBytes,
			OutError);
	}

	bool DecodeTestState(
		const FSeinSnapshotLatentActionRecord& Record,
		FSeinMoveToActionContinuation& OutState,
		FString& OutError)
	{
		if (Record.StableCodecID
				!= TEXT("seinarts.movement.move-to")
			|| Record.ActionClassPath
				!= USeinMoveToAction::StaticClass()
					->GetPathName())
		{
			OutError =
				TEXT("Move To test mutation requires the exact codec record.");
			return false;
		}
		const TArray<const UScriptStruct*> DynamicStructs;
		const TArray<FName> Names;
		return FSeinCanonicalStateCodec::Decode(
			Record.PayloadBytes,
			FSeinMoveToActionContinuation::StaticStruct(),
			&OutState,
			{ DynamicStructs, Names },
			MoveToTestWireLimits(),
			OutError);
	}
}

void UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	SeedEveryMappedField(
		USeinMoveToAction& Action,
		USeinMovement* Movement)
{
	Action.Destination = FFixedVector(
		FFixedPoint::FromInt(101),
		FFixedPoint::FromInt(202),
		FFixedPoint::FromInt(303));
	Action.AcceptanceRadius =
		FFixedPoint::FromInt(404);
	Action.CurrentWaypointIndex = 1;
	Action.bPathResolved = true;
	Action.bAuthoritativeDestination = true;
	Action.PathOriginAgentPos = FFixedVector(
		FFixedPoint::FromInt(11),
		FFixedPoint::FromInt(12),
		FFixedPoint::FromInt(13));
	Action.TimeSinceLastRepath =
		FFixedPoint::FromInt(14);
	Action.ConsecutiveRepathFailures = 15;
	Action.BestDistToFinal =
		FFixedPoint::FromInt(16);
	Action.TimeStalledNearGoal =
		FFixedPoint::FromInt(17);
	// Deliberately NOT a shape the validator would accept (Escaping with a live
	// order path, movement-finalized): this seed exists only to give every
	// mapped field a distinct non-default value for the Read/Apply round-trip.
	Action.StuckPhase = ESeinMoveStuckPhase::Escaping;
	Action.HoldTime = FFixedPoint::FromInt(18);
	Action.HoldBoundariesFired = 19;
	Action.bForceRepathNow = true;
	Action.EscapeOrigin = FFixedVector(
		FFixedPoint::FromInt(51),
		FFixedPoint::FromInt(52),
		FFixedPoint::FromInt(53));
	Action.EscapeTarget = FFixedVector(
		FFixedPoint::FromInt(21),
		FFixedPoint::FromInt(22),
		FFixedPoint::FromInt(23));
	Action.EscapeAcceptanceRadius =
		FFixedPoint::FromInt(24);
	Action.EscapeHoldTime =
		FFixedPoint::FromInt(25);
	Action.EscapeAttempts = 26;
	Action.TotalEscapeEntries = 27;
	Action.FootprintRadius =
		FFixedPoint::FromInt(28);
	Action.StallBand =
		FFixedPoint::FromInt(29);
	Action.Path = FSeinPath();
	Action.Path.Waypoints = {
		FFixedVector(
			FFixedPoint::FromInt(31),
			FFixedPoint::FromInt(32),
			FFixedPoint::FromInt(33)),
		FFixedVector(
			FFixedPoint::FromInt(34),
			FFixedPoint::FromInt(35),
			FFixedPoint::FromInt(36)),
		FFixedVector(
			FFixedPoint::FromInt(37),
			FFixedPoint::FromInt(38),
			FFixedPoint::FromInt(39)),
	};
	FSeinPathSegment Arc;
	Arc.Type = ESeinPathSegmentType::Arc;
	Arc.From = Action.Path.Waypoints[0];
	Arc.To = Action.Path.Waypoints[1];
	Arc.Center = FFixedVector(
		FFixedPoint::FromInt(41),
		FFixedPoint::FromInt(42),
		FFixedPoint::FromInt(43));
	Arc.Radius = FFixedPoint::FromInt(44);
	Arc.SweepAngle = -FFixedPoint::FromInt(45);
	Arc.bReverse = true;
	Action.Path.Segments.Add(Arc);
	Action.Path.TotalCost = FFixedPoint::FromInt(46);
	Action.Path.bIsValid = true;
	Action.Path.bIsPartial = true;
	Action.Path.DebugCellPath.Add(
		FFixedVector(
			FFixedPoint::FromInt(47),
			FFixedPoint::FromInt(48),
			FFixedPoint::FromInt(49)));
	Action.Movement = Movement;
	Action.bMovementFinalized = true;

	// Deliberately non-default diagnostics prove that the mapping omits them.
	Action.InitialThrottleStreak = 50;
}

USeinMoveToAction* UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	CloneMappedFields(
		const USeinMoveToAction& Source,
		UObject& Outer)
{
	FSeinMoveToActionContinuation State;
	FSeinMoveToActionCodec::ReadActionState(
		Source, State);
	USeinMoveToAction* Clone =
		NewObject<USeinMoveToAction>(&Outer);
	if (!Clone)
	{
		return nullptr;
	}
	FSeinMoveToActionCodec::ApplyActionState(
		State, *Clone);
	Clone->Movement = State.bHasMovementBinding
		? Source.Movement
		: nullptr;
	return Clone;
}

bool UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	MappedFieldsEqual(
		const USeinMoveToAction& A,
		const USeinMoveToAction& B,
		FString& OutError)
{
	FSeinMoveToActionContinuation StateA;
	FSeinMoveToActionContinuation StateB;
	FSeinMoveToActionCodec::ReadActionState(A, StateA);
	FSeinMoveToActionCodec::ReadActionState(B, StateB);
	TArray<uint8> BytesA;
	TArray<uint8> BytesB;
	return EncodeTestState(StateA, BytesA, OutError)
		&& EncodeTestState(StateB, BytesB, OutError)
		&& BytesA == BytesB;
}

bool UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	DiagnosticsWereReset(
		const USeinMoveToAction& Action)
{
	return Action.InitialThrottleStreak == 0
		&& Action.Path.DebugCellPath.IsEmpty();
}

USeinMoveToProxy* UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetObserver(const USeinMoveToAction& Action)
{
	return Action.Observer.Get();
}

USeinMoveToAction* UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetRunningAction(const USeinMoveToProxy& Proxy)
{
	return Proxy.RunningAction;
}

bool UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	IsSilentlyDetached(const USeinMoveToProxy& Proxy)
{
	return !Proxy.RunningAction
		&& !Proxy.CachedAbility
		&& !Proxy.OnCompleted.IsBound()
		&& !Proxy.OnFailed.IsBound()
		&& !Proxy.OnWaypointReached.IsBound()
		&& !Proxy.OnCancelled.IsBound()
		&& !Proxy.OnPartialPath.IsBound()
		&& !Proxy.OnPathRecomputed.IsBound();
}

void UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	SetForceRepathPending(
		USeinMoveToAction& Action,
		bool bPending)
{
	Action.bForceRepathNow = bPending;
}

bool UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	IsForceRepathPending(const USeinMoveToAction& Action)
{
	return Action.bForceRepathNow;
}

FFixedPoint UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetRepathElapsed(const USeinMoveToAction& Action)
{
	return Action.TimeSinceLastRepath;
}

FFixedPoint UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetHoldTime(const USeinMoveToAction& Action)
{
	return Action.HoldTime;
}

int32 UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetHoldBoundariesFired(const USeinMoveToAction& Action)
{
	return Action.HoldBoundariesFired;
}

int32 UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	GetEscapeAttempts(const USeinMoveToAction& Action)
{
	return Action.EscapeAttempts;
}

void UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	SetTotalEscapeEntries(
		USeinMoveToAction& Action,
		int32 Entries)
{
	Action.TotalEscapeEntries = Entries;
}

bool UE::SeinARTSTests::
	FMoveToActionContinuationTestAccess::
	TickRepathWithoutNavigationSubsystem(
		USeinMoveToAction& Action,
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World)
{
	FSeinEntity* Entity = World.GetEntityMutable(Action.OwnerEntity);
	FSeinMovementPayload* MovementData =
		World.GetComponentMutable<FSeinMovementPayload>(
			Action.OwnerEntity);
	const FSeinNavigationPayload* NavigationData =
		World.GetComponent<FSeinNavigationPayload>(
			Action.OwnerEntity);
	USeinNavigation* Navigation =
		USeinNavigationSubsystem::GetNavigationForWorld(&World);
	if (!Entity || !MovementData || !NavigationData || !Navigation)
	{
		return false;
	}
	return Action.TickRepath(
			DeltaTime,
			World,
			*Entity,
			*MovementData,
			NavigationData,
			Navigation,
			nullptr)
		!= USeinMoveToAction::ERepathTickResult::Terminal;
}

bool UE::SeinARTSTests::
	MutateMoveToContinuationForTest(
		FSeinSnapshotLatentActionRecord& Record,
		EMoveToContinuationMutation Mutation,
		FString& OutError)
{
	OutError.Reset();
	FSeinMoveToActionContinuation State;
	if (!DecodeTestState(Record, State, OutError))
	{
		return false;
	}

	switch (Mutation)
	{
	case EMoveToContinuationMutation::
		InvalidWaypointCursor:
		State.CurrentWaypointIndex = MAX_int32;
		break;
	case EMoveToContinuationMutation::
		ResolvedWithoutMovement:
		State.bPathResolved = true;
		State.bHasMovementBinding = false;
		break;
	case EMoveToContinuationMutation::
		NonstandardCompletedRoute:
		State.OnCompletedFunction =
			TEXT("HandleCompleted");
		break;
	case EMoveToContinuationMutation::
		EscapeCounterOutsideBound:
		State.EscapeAttempts = MAX_int32;
		break;
	case EMoveToContinuationMutation::
		EscapingWithOrderPath:
		// The captured record is a resolved, moving order: flipping only
		// the phase leaves its path in place, which Escaping forbids.
		State.StuckPhase = ESeinMoveStuckPhase::Escaping;
		break;
	case EMoveToContinuationMutation::
		FreeWithHoldClock:
		State.StuckPhase = ESeinMoveStuckPhase::Free;
		State.HoldTime = FFixedPoint::One;
		break;
	default:
		OutError =
			TEXT("Unknown Move To continuation test mutation.");
		return false;
	}
	return EncodeTestState(
		State, Record.PayloadBytes, OutError);
}

bool UE::SeinARTSTests::
	ReplaceMoveToCompletedRouteForTest(
		FSeinSnapshotLatentActionRecord& Record,
		const FString& ReplacementFunction,
		FString& OutError)
{
	OutError.Reset();
	if (ReplacementFunction.IsEmpty())
	{
		OutError =
			TEXT("Move To replacement route must be non-empty.");
		return false;
	}
	FSeinMoveToActionContinuation State;
	if (!DecodeTestState(Record, State, OutError))
	{
		return false;
	}
	State.OnCompletedFunction = ReplacementFunction;
	return EncodeTestState(
		State, Record.PayloadBytes, OutError);
}

#endif
