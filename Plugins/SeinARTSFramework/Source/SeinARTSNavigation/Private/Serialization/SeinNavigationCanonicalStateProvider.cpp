/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationCanonicalStateProvider.cpp
 * @brief   Reload-safe capture and restore of deferred pathfinding work.
 */

#include "Serialization/SeinNavigationCanonicalStateProvider.h"

#include "Serialization/SeinNavigationCanonicalState.h"
#include "SeinNavigationSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "Math/MathLib.h"

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSNavigation"));

	USeinNavigationSubsystem* ResolveSubsystem(
		const USeinWorldSubsystem& Services)
	{
		UWorld* World = Services.GetWorld();
		return World
			? World->GetSubsystem<USeinNavigationSubsystem>()
			: nullptr;
	}

	bool IsStrictlyAfter(
		const FSeinEntityHandle& Previous,
		const FSeinEntityHandle& Current)
	{
		return Previous < Current;
	}

	bool ValidateRequestOrder(
		TConstArrayView<FSeinPathRequest> Requests,
		const TCHAR* Lane,
		FString& OutError)
	{
		FSeinEntityHandle Previous;
		for (int32 Index = 0; Index < Requests.Num(); ++Index)
		{
			const FSeinEntityHandle Requester = Requests[Index].Requester;
			if (!Requester.IsValid()
				|| (Index > 0 && !IsStrictlyAfter(Previous, Requester)))
			{
				OutError = FString::Printf(
					TEXT("Navigation %s requests must have valid, unique requesters in strict canonical order."),
					Lane);
				return false;
			}
			Previous = Requester;
		}
		return true;
	}

	bool ValidateRequest(
		const FSeinPathRequest& Request,
		const TCHAR* Lane,
		FString& OutError)
	{
		if (Request.AgentFootprintRadius < FFixedPoint::Zero
			|| Request.AgentWallPaddingCells < 0
			|| Request.AgentMaxSearchNodes < 0)
		{
			OutError = FString::Printf(
				TEXT("Navigation %s request for entity %s has negative agent limits."),
				Lane,
				*Request.Requester.ToString());
			return false;
		}
		return true;
	}

	/**
	 * Return an overflow-resistant planar magnitude using only deterministic
	 * fixed-point operations. Scaling by the largest component keeps the
	 * squared terms in [0, 1] instead of overflowing on ordinary world-space
	 * coordinates.
	 */
	bool TryComputePlanarMagnitude(
		FFixedVector Value,
		FFixedPoint& OutMagnitude)
	{
		Value.Z = FFixedPoint::Zero;
		if (Value.X == FFixedPoint::MinValue
			|| Value.Y == FFixedPoint::MinValue)
		{
			return false;
		}

		const FFixedPoint AbsX =
			Value.X < FFixedPoint::Zero ? -Value.X : Value.X;
		const FFixedPoint AbsY =
			Value.Y < FFixedPoint::Zero ? -Value.Y : Value.Y;
		const FFixedPoint Scale = AbsX > AbsY ? AbsX : AbsY;
		if (Scale == FFixedPoint::Zero)
		{
			OutMagnitude = FFixedPoint::Zero;
			return true;
		}

		const FFixedPoint NormalizedX = AbsX / Scale;
		const FFixedPoint NormalizedY = AbsY / Scale;
		const FFixedPoint NormalizedMagnitude = SeinMath::Sqrt(
			NormalizedX * NormalizedX
			+ NormalizedY * NormalizedY);
		if (NormalizedMagnitude <= FFixedPoint::Zero
			|| Scale > FFixedPoint::MaxValue / NormalizedMagnitude)
		{
			return false;
		}

		OutMagnitude = Scale * NormalizedMagnitude;
		return OutMagnitude >= FFixedPoint::Zero;
	}

	bool TryComputeFixedDelta(
		const FFixedPoint A,
		const FFixedPoint B,
		FFixedPoint& OutDelta)
	{
		const uint64 ABits =
			static_cast<uint64>(A.Value);
		const uint64 BBits =
			static_cast<uint64>(B.Value);
		const uint64 DifferenceBits = ABits - BBits;
		constexpr uint64 SignBit = 1ull << 63;
		if (((ABits ^ BBits)
				& (ABits ^ DifferenceBits)
				& SignBit) != 0)
		{
			return false;
		}
		OutDelta = FFixedPoint(
			BitCast<int64>(DifferenceBits));
		return true;
	}

	bool TryComputePlanarOffset(
		const FFixedVector& Point,
		const FFixedVector& Origin,
		FFixedVector& OutOffset)
	{
		OutOffset.Z = FFixedPoint::Zero;
		return TryComputeFixedDelta(
				Point.X, Origin.X, OutOffset.X)
			&& TryComputeFixedDelta(
				Point.Y, Origin.Y, OutOffset.Y);
	}

	/**
	 * Preserve the legacy non-Arc cost calculation exactly inside its valid
	 * domain, but prove both coordinate subtraction and the fixed-point
	 * square/sum are representable before performing them.
	 */
	bool TryComputeRepresentablePlanarLength(
		const FFixedVector& To,
		const FFixedVector& From,
		FFixedPoint& OutLength)
	{
		FFixedVector Delta;
		if (!TryComputePlanarOffset(To, From, Delta)
			|| Delta.X == FFixedPoint::MinValue
			|| Delta.Y == FFixedPoint::MinValue)
		{
			return false;
		}

		const FFixedPoint AbsX =
			Delta.X < FFixedPoint::Zero
				? -Delta.X
				: Delta.X;
		const FFixedPoint AbsY =
			Delta.Y < FFixedPoint::Zero
				? -Delta.Y
				: Delta.Y;
		const FFixedPoint MaxSafeComponent =
			SeinMath::Sqrt(FFixedPoint::MaxValue);
		if (AbsX > MaxSafeComponent
			|| AbsY > MaxSafeComponent)
		{
			return false;
		}

		const FFixedPoint XSquared = Delta.X * Delta.X;
		const FFixedPoint YSquared = Delta.Y * Delta.Y;
		if (XSquared < FFixedPoint::Zero
			|| YSquared < FFixedPoint::Zero
			|| XSquared
				> FFixedPoint::MaxValue - YSquared)
		{
			return false;
		}

		const FFixedPoint PlanarSizeSquared =
			XSquared + YSquared;
		OutLength = SeinMath::Sqrt(PlanarSizeSquared);
		return OutLength >= FFixedPoint::Zero;
	}

	/**
	 * Arc producers and consumers use SeinMath's 1024-sample quarter-wave
	 * sine table. A 4 / table-size relative-radius budget (Radius / 256
	 * today) plus a 1/1024-world-unit absolute floor conservatively exceeds
	 * one table bin's angular chord error and covers 32.32 composition
	 * rounding while still rejecting materially malformed restored geometry.
	 * The formula is fixed-point-only, so every peer makes the same
	 * accept/reject decision.
	 */
	FFixedPoint ArcGeometryTolerance(const FFixedPoint Radius)
	{
		static_assert(SeinMath::SIN_TABLE_SIZE >= 4);
		const FFixedPoint AbsoluteFloor =
			FFixedPoint::One / FFixedPoint::FromInt(1024);
		const FFixedPoint RelativeTolerance =
			Radius / FFixedPoint::FromInt(
				SeinMath::SIN_TABLE_SIZE / 4);
		return RelativeTolerance > AbsoluteFloor
			? RelativeTolerance
			: AbsoluteFloor;
	}

	bool IsOutsideTolerance(
		const FFixedPoint A,
		const FFixedPoint B,
		const FFixedPoint Tolerance)
	{
		const uint64 Difference =
			A >= B
				? static_cast<uint64>(A.Value)
					- static_cast<uint64>(B.Value)
				: static_cast<uint64>(B.Value)
					- static_cast<uint64>(A.Value);
		return Difference > static_cast<uint64>(Tolerance.Value);
	}

	bool ValidatePath(
		const FSeinPath& Path,
		const FSeinPathRequest& Request,
		FString& OutError)
	{
		const FSeinEntityHandle Requester = Request.Requester;
		if (Path.TotalCost < FFixedPoint::Zero)
		{
			OutError = FString::Printf(
				TEXT("Navigation ready path for entity %s has a negative total cost."),
				*Requester.ToString());
			return false;
		}
		if (!Path.bIsValid)
		{
			if (!Path.Waypoints.IsEmpty()
				|| !Path.Segments.IsEmpty()
				|| Path.TotalCost != FFixedPoint::Zero
				|| Path.bIsPartial)
			{
				OutError = FString::Printf(
					TEXT("Navigation invalid ready path for entity %s is not in the canonical cleared state."),
					*Requester.ToString());
				return false;
			}
			return true;
		}
		if (Path.Waypoints.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Navigation valid ready path for entity %s has no drivable waypoint."),
				*Requester.ToString());
			return false;
		}
		if (!Path.bIsPartial
			&& Path.Waypoints.Last() != Request.End)
		{
			OutError = FString::Printf(
				TEXT("Navigation complete ready path for entity %s does not terminate at its requested destination."),
				*Requester.ToString());
			return false;
		}
		if (Path.bIsPartial
			&& Path.Waypoints.Last() == Request.End)
		{
			OutError = FString::Printf(
				TEXT("Navigation partial ready path for entity %s already terminates at its requested destination."),
				*Requester.ToString());
			return false;
		}

		bool bAllStraight = true;
		FFixedPoint ExpectedTotalCost = FFixedPoint::Zero;
		for (int32 Index = 0; Index < Path.Segments.Num(); ++Index)
		{
			const FSeinPathSegment& Segment = Path.Segments[Index];
			if (static_cast<uint8>(Segment.Type)
					> static_cast<uint8>(ESeinPathSegmentType::Jump))
			{
				OutError = FString::Printf(
					TEXT("Navigation ready path for entity %s contains an unknown segment kind."),
					*Requester.ToString());
				return false;
			}
			if (Index > 0
				&& Path.Segments[Index - 1].To != Segment.From)
			{
				OutError = FString::Printf(
					TEXT("Navigation ready path for entity %s has disconnected typed segments."),
					*Requester.ToString());
				return false;
			}
			if (Segment.Type == ESeinPathSegmentType::Arc)
			{
				if (Segment.Radius <= FFixedPoint::Zero
					|| Segment.SweepAngle == FFixedPoint::Zero)
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has a degenerate arc segment."),
						*Requester.ToString());
					return false;
				}
				const FFixedPoint AbsSweep =
					Segment.SweepAngle < FFixedPoint::Zero
						? -Segment.SweepAngle
						: Segment.SweepAngle;
				if (AbsSweep <= FFixedPoint::Zero)
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has a non-representable arc sweep."),
						*Requester.ToString());
					return false;
				}

				const FFixedPoint GeometryTolerance =
					ArcGeometryTolerance(Segment.Radius);
				FFixedVector FromRadial;
				FFixedVector ToRadial;
				FFixedPoint FromRadius;
				FFixedPoint ToRadius;
				if (!TryComputePlanarOffset(
						Segment.From,
						Segment.Center,
						FromRadial)
					|| !TryComputePlanarOffset(
						Segment.To,
						Segment.Center,
						ToRadial)
					|| !TryComputePlanarMagnitude(
						FromRadial, FromRadius)
					|| !TryComputePlanarMagnitude(
						ToRadial, ToRadius))
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has arc coordinates outside the representable distance domain."),
						*Requester.ToString());
					return false;
				}
				if (IsOutsideTolerance(
						FromRadius,
						Segment.Radius,
						GeometryTolerance))
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has an arc From endpoint outside its declared planar radius."),
						*Requester.ToString());
					return false;
				}
				if (IsOutsideTolerance(
						ToRadius,
						Segment.Radius,
						GeometryTolerance))
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has an arc To endpoint outside its declared planar radius."),
						*Requester.ToString());
					return false;
				}

				const FFixedPoint SweepSin =
					SeinMath::Sin(Segment.SweepAngle);
				const FFixedPoint SweepCos =
					SeinMath::Cos(Segment.SweepAngle);
				const FFixedPoint ExpectedToX =
					FromRadial.X * SweepCos
					- FromRadial.Y * SweepSin;
				const FFixedPoint ExpectedToY =
					FromRadial.X * SweepSin
					+ FromRadial.Y * SweepCos;
				if (IsOutsideTolerance(
						ToRadial.X,
						ExpectedToX,
						GeometryTolerance)
					|| IsOutsideTolerance(
						ToRadial.Y,
						ExpectedToY,
						GeometryTolerance))
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has an arc signed sweep that does not reach its declared endpoint."),
						*Requester.ToString());
					return false;
				}

				ExpectedTotalCost += Segment.Radius * AbsSweep;
				bAllStraight = false;
			}
			else
			{
				if (Segment.Center != FFixedVector::ZeroVector
					|| Segment.Radius != FFixedPoint::Zero
					|| Segment.SweepAngle != FFixedPoint::Zero)
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s stores arc geometry on a non-arc segment."),
						*Requester.ToString());
					return false;
				}
				FFixedPoint PlanarLength;
				if (!TryComputeRepresentablePlanarLength(
						Segment.To,
						Segment.From,
						PlanarLength)
					|| ExpectedTotalCost
						> FFixedPoint::MaxValue
							- PlanarLength)
				{
					OutError = FString::Printf(
						TEXT("Navigation ready path for entity %s has segment coordinates outside the representable distance domain."),
						*Requester.ToString());
					return false;
				}
				ExpectedTotalCost += PlanarLength;
				bAllStraight &=
					Segment.Type == ESeinPathSegmentType::Straight;
			}
		}
		if (Path.TotalCost != ExpectedTotalCost)
		{
			OutError = FString::Printf(
				TEXT("Navigation ready path for entity %s has an inconsistent total cost."),
				*Requester.ToString());
			return false;
		}

		if (bAllStraight)
		{
			const int32 ExpectedSegments =
				FMath::Max(0, Path.Waypoints.Num() - 1);
			if (Path.Segments.Num() != ExpectedSegments)
			{
				OutError = FString::Printf(
					TEXT("Navigation straight ready path for entity %s does not match its waypoint backbone."),
					*Requester.ToString());
				return false;
			}
			for (int32 Index = 0; Index < Path.Segments.Num(); ++Index)
			{
				if (Path.Segments[Index].From != Path.Waypoints[Index]
					|| Path.Segments[Index].To
						!= Path.Waypoints[Index + 1])
				{
					OutError = FString::Printf(
						TEXT("Navigation straight ready path for entity %s disagrees with its waypoint endpoints."),
						*Requester.ToString());
					return false;
				}
			}
		}
		else if (Path.Segments.IsEmpty()
			|| Path.Waypoints[0] != Path.Segments[0].From
			|| Path.Waypoints.Last() != Path.Segments.Last().To)
		{
			OutError = FString::Printf(
				TEXT("Navigation typed ready path for entity %s disagrees with its terminal waypoints."),
				*Requester.ToString());
			return false;
		}
		return true;
	}

	bool ValidateState(
		const FSeinNavigationContinuationState& State,
		int32 Tick,
		FString& OutError)
	{
		// The sync budget resets lazily at the next RequestPath call, not when
		// the world tick advances. A non-zero count from LastResetTick < Tick
		// is therefore valid idle-gap state; -1 alone proves no reset/request
		// has ever occurred and must retain the zero count.
		if (State.PathRequestsThisTick < 0
			|| State.LastResetTick < -1
			|| State.LastDrainTick < -1
			|| State.LastResetTick > Tick
			|| State.LastDrainTick > Tick
			|| (State.LastResetTick == -1
				&& State.PathRequestsThisTick != 0)
			|| !ValidateRequestOrder(
				State.QueuedRequests, TEXT("queued"), OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Navigation continuation counters are invalid.");
			}
			return false;
		}

		for (const FSeinPathRequest& Request : State.QueuedRequests)
		{
			if (!ValidateRequest(Request, TEXT("queued"), OutError))
			{
				return false;
			}
		}

		FSeinEntityHandle Previous;
		for (int32 Index = 0; Index < State.ReadyResults.Num(); ++Index)
		{
			const FSeinNavigationAsyncResultState& Result =
				State.ReadyResults[Index];
			const FSeinEntityHandle Requester =
				Result.Request.Requester;
			if (!Requester.IsValid()
				|| (Index > 0 && !IsStrictlyAfter(Previous, Requester)))
			{
				OutError =
					TEXT("Navigation ready results must have valid, unique requesters in strict canonical order.");
				return false;
			}
			if (!ValidateRequest(
					Result.Request, TEXT("ready"), OutError)
				|| !ValidatePath(
					Result.Path, Result.Request, OutError))
			{
				return false;
			}
			Previous = Requester;
		}

		int32 QueuedIndex = 0;
		int32 ReadyIndex = 0;
		while (QueuedIndex < State.QueuedRequests.Num()
			&& ReadyIndex < State.ReadyResults.Num())
		{
			const FSeinEntityHandle QueuedRequester =
				State.QueuedRequests[QueuedIndex].Requester;
			const FSeinEntityHandle ReadyRequester =
				State.ReadyResults[ReadyIndex].Request.Requester;
			if (QueuedRequester == ReadyRequester)
			{
				OutError =
					TEXT("Navigation continuation cannot queue and publish a result for the same requester.");
				return false;
			}
			if (QueuedRequester < ReadyRequester)
			{
				++QueuedIndex;
			}
			else
			{
				++ReadyIndex;
			}
		}
		return true;
	}

}

struct FSeinNavigationRestoreStage final
	: ISeinCanonicalStateRestoreStage
{
	TWeakObjectPtr<USeinNavigationSubsystem> Subsystem;
	FString BindingFrame;
	FGuid StaticEnvironmentDigest;
	FSeinNavigationContinuationState State;

	virtual bool VerifyExternalLeases(
		FString& OutError) const override
	{
		USeinNavigationSubsystem* NavigationSubsystem =
			Subsystem.Get();
		if (!NavigationSubsystem)
		{
			OutError =
				TEXT("Navigation world subsystem disappeared during restore staging.");
			return false;
		}
		return NavigationSubsystem->
			RevalidateCanonicalStateBindingCandidate(
				BindingFrame,
				StaticEnvironmentDigest,
				OutError);
	}
};

struct FSeinNavigationCanonicalStateProvider
{
	static bool PrepareWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutError)
	{
		USeinNavigationSubsystem* NavigationSubsystem =
			ResolveSubsystem(Context.Services);
		if (!NavigationSubsystem)
		{
			OutError =
				TEXT("Navigation canonical state could not resolve its world subsystem.");
			return false;
		}
		return NavigationSubsystem->
			PrepareInitialCanonicalStateEnvironment(OutError);
	}

	static bool FreezeWorldBinding(
		const FSeinCanonicalStateWorldBindingContext& Context,
		FString& OutFrame,
		FString& OutError)
	{
		USeinNavigationSubsystem* NavigationSubsystem =
			ResolveSubsystem(Context.Services);
		if (!NavigationSubsystem)
		{
			OutError =
				TEXT("Navigation canonical state could not resolve its world subsystem.");
			return false;
		}
		FGuid StaticEnvironmentDigest;
		return NavigationSubsystem->FreezeCanonicalStateBinding(
			Context.BindingDisposition
				== ESeinCanonicalStateWorldBindingDisposition::
					BootstrapCommit,
			OutFrame,
			StaticEnvironmentDigest,
			OutError);
	}

	static bool Capture(
		const FSeinCanonicalStateCaptureContext& Context,
		FInstancedStruct& OutState,
		FString& OutError)
	{
		USeinNavigationSubsystem* NavigationSubsystem =
			ResolveSubsystem(Context.World);
		if (!NavigationSubsystem)
		{
			OutError =
				TEXT("Navigation canonical-state capture could not resolve its world subsystem.");
			return false;
		}

		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		if (!NavigationSubsystem->FreezeCanonicalStateBinding(
			false,
			BindingFrame,
			StaticEnvironmentDigest,
			OutError))
		{
			return false;
		}

		FSeinNavigationContinuationState State;
		State.PathRequestsThisTick =
			NavigationSubsystem->PathRequestsThisTick;
		State.LastResetTick = NavigationSubsystem->LastResetTick;
		State.LastDrainTick = NavigationSubsystem->LastDrainTick;

		TArray<FSeinEntityHandle> QueueKeys;
		NavigationSubsystem->AsyncQueue.GetKeys(QueueKeys);
		QueueKeys.Sort();
		State.QueuedRequests.Reserve(QueueKeys.Num());
		for (const FSeinEntityHandle Requester : QueueKeys)
		{
			const FSeinPathRequest& Request =
				NavigationSubsystem->AsyncQueue.FindChecked(Requester);
			if (Request.Requester != Requester)
			{
				OutError =
					TEXT("Navigation async queue key and request identity disagree.");
				return false;
			}
			State.QueuedRequests.Add(Request);
		}

		TArray<FSeinEntityHandle> ResultKeys;
		NavigationSubsystem->AsyncResults.GetKeys(ResultKeys);
		ResultKeys.Sort();
		State.ReadyResults.Reserve(ResultKeys.Num());
		for (const FSeinEntityHandle Requester : ResultKeys)
		{
			const USeinNavigationSubsystem::FSeinAsyncPathResult& Result =
				NavigationSubsystem->AsyncResults.FindChecked(Requester);
			if (Result.Request.Requester != Requester)
			{
				OutError =
					TEXT("Navigation async result key and request identity disagree.");
				return false;
			}
			FSeinNavigationAsyncResultState& Record =
				State.ReadyResults.AddDefaulted_GetRef();
			Record.Request = Result.Request;
			Record.Path = Result.Path;
		}

		if (!ValidateState(State, Context.Tick, OutError))
		{
			return false;
		}
		OutState = FInstancedStruct::Make(MoveTemp(State));
		return true;
	}

	static bool StageRestore(
		const FSeinCanonicalStateStageContext& Context,
		const FInstancedStruct& State,
		TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
		FString& OutError)
	{
		if (!Context.Services)
		{
			OutError =
				TEXT("Navigation canonical restore requires read-only world services.");
			return false;
		}
		USeinNavigationSubsystem* NavigationSubsystem =
			ResolveSubsystem(*Context.Services);
		FString BindingFrame;
		FGuid StaticEnvironmentDigest;
		if (!NavigationSubsystem
			|| !NavigationSubsystem->FreezeCanonicalStateBinding(
				false,
				BindingFrame,
				StaticEnvironmentDigest,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Navigation canonical restore could not validate its frozen world binding.");
			}
			return false;
		}

		const FSeinNavigationContinuationState* Payload =
			State.GetPtr<FSeinNavigationContinuationState>();
		if (!Payload
			|| !ValidateState(*Payload, Context.Tick, OutError))
		{
			if (!Payload)
			{
				OutError =
					TEXT("Navigation canonical-state payload has the wrong root type.");
			}
			return false;
		}

		TUniquePtr<FSeinNavigationRestoreStage> Stage =
			MakeUnique<FSeinNavigationRestoreStage>();
		Stage->Subsystem = NavigationSubsystem;
		Stage->BindingFrame = MoveTemp(BindingFrame);
		Stage->StaticEnvironmentDigest =
			StaticEnvironmentDigest;
		Stage->State = *Payload;
		OutStage = MoveTemp(Stage);
		return true;
	}

	static void CommitRestore(
		FSeinCanonicalStateCommitContext& Context,
		TUniquePtr<ISeinCanonicalStateRestoreStage>&& OpaqueStage)
	{
		FSeinNavigationRestoreStage* Stage =
			static_cast<FSeinNavigationRestoreStage*>(
				OpaqueStage.Get());
		check(Stage);
		UWorld* UnrealWorld = Context.World.GetWorld();
		USeinNavigationSubsystem* NavigationSubsystem =
			UnrealWorld
				? UnrealWorld->GetSubsystem<USeinNavigationSubsystem>()
				: nullptr;
		check(NavigationSubsystem);
		NavigationSubsystem->CommitCanonicalStateBinding(
			Stage->BindingFrame,
			Stage->StaticEnvironmentDigest);

		NavigationSubsystem->PathRequestsThisTick =
			Stage->State.PathRequestsThisTick;
		NavigationSubsystem->LastResetTick =
			Stage->State.LastResetTick;
		NavigationSubsystem->LastDrainTick =
			Stage->State.LastDrainTick;
		NavigationSubsystem->AsyncQueue.Reset();
		NavigationSubsystem->AsyncResults.Reset();

		for (const FSeinPathRequest& Request :
			Stage->State.QueuedRequests)
		{
			NavigationSubsystem->AsyncQueue.Add(
				Request.Requester, Request);
		}
		for (const FSeinNavigationAsyncResultState& Record :
			Stage->State.ReadyResults)
		{
			USeinNavigationSubsystem::FSeinAsyncPathResult Result;
			Result.Request = Record.Request;
			Result.Path = Record.Path;
			NavigationSubsystem->AsyncResults.Add(
				Record.Request.Requester, MoveTemp(Result));
		}
	}
};

FSeinCanonicalStateRegistrationHandle
SeinRegisterNavigationCanonicalStateProvider(FString& OutError)
{
	FSeinCanonicalStateDescriptor Descriptor;
	Descriptor.Key.StableDomainId = TEXT("seinarts.navigation");
	Descriptor.Key.StableContributorId =
		TEXT("async-path-continuation");
	Descriptor.SchemaVersion = 1;
	Descriptor.ImplementationRevision = 3;
	Descriptor.Role = ESeinCanonicalStateRole::Continuation;
	Descriptor.PayloadStruct =
		FSeinNavigationContinuationState::StaticStruct();
	Descriptor.Limits.MaxRecursionDepth = 64;
	Descriptor.Limits.MaxEncodedBytes = 16 * 1024 * 1024;
	Descriptor.Limits.MaxAggregateElements = 1024 * 1024;
	// The nav subsystem owns this contributor's lifecycle: the blocker-stamp
	// system that names it registers only in nav-enabled worlds, and a
	// nav-disabled world must still bootstrap with the contributor present.
	Descriptor.bExternallyOwned = true;

	FSeinCanonicalStateContributorOps Ops;
	Ops.PrepareWorldBinding =
		&FSeinNavigationCanonicalStateProvider::PrepareWorldBinding;
	Ops.FreezeWorldBinding =
		&FSeinNavigationCanonicalStateProvider::FreezeWorldBinding;
	Ops.Capture = &FSeinNavigationCanonicalStateProvider::Capture;
	Ops.StageRestore =
		&FSeinNavigationCanonicalStateProvider::StageRestore;
	Ops.CommitRestore =
		&FSeinNavigationCanonicalStateProvider::CommitRestore;
	return FSeinCanonicalStateRegistry::Register(
		OwnerModuleId, Descriptor, MoveTemp(Ops), &OutError);
}
