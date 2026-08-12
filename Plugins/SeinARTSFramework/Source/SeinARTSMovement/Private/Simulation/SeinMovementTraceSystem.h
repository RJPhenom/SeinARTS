/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementTraceSystem.h
 * @brief   Observation-only movement trace: the "written picture" of a crowd jam.
 *
 *          Silent by default. Enable in PIE with `log LogSeinMoveTrace Verbose`,
 *          reproduce, and paste the log. The system runs LAST in PostTick (after
 *          collision resolution and nav containment) so it sees the tick's FINAL
 *          transforms, and attributes each commanded unit's motion three ways:
 *
 *            commanded  = FSeinMovementComponent::Velocity — the unit's OWN
 *                         movement step this tick (post nav-floor, pre body
 *                         collision; the movement harness writes it and the
 *                         collision resolver never does),
 *            actual     = net world displacement since last PostTick,
 *            collision  = actual − commanded — everything the PostTick
 *                         resolver/containment did to the unit.
 *
 *          A unit commanding full speed while its body goes nowhere (a PRESSER)
 *          is invisible to every velocity-gated consumer — this trace is the only
 *          place that population is measurable.
 *
 *          Output grammar (greppable tags, one line each):
 *            [EP] start/agg/end  — jam episode lifecycle + 0.5s aggregate rows
 *            [UNIT]              — per-unit deep sample (worst offenders)
 *            [ORPHAN]            — bHasTarget=true with NO live latent action
 *          plus [ARRIVE]/[THROTTLE] event lines emitted by the harness/action
 *          under the same log category.
 *
 *          DETERMINISM: pure observation. Reads sim state, writes only its own
 *          transient (non-authoritative) bookkeeping and the log. Registered on every
 *          client unconditionally; a client with the channel silent simply logs
 *          nothing — authoritative sim state is untouched either way.
 *
 * Phase: FinalObservation | Priority: MovementTrace (10), after presentation
 * sampling and every authoritative PostTick system.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Settings/PluginSettings.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMovementTraceLog.h"

class FSeinMovementTraceSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
#if !UE_BUILD_SHIPPING
		if (!UE_LOG_ACTIVE(LogSeinMoveTrace, Verbose))
		{
			// Channel silent → drop all bookkeeping so a mid-session enable starts fresh.
			if (States.Num() > 0) { States.Empty(); ResetEpisode(); }
			return;
		}
		if (DeltaTime <= FFixedPoint::Zero) return;

		const int32 Tick = World.GetCurrentTick();
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const FFixedPoint PinFloor = Settings->AvoidanceMovingSpeedFloor;

		const ISeinComponentStorage* MoveStorage =
			World.GetComponentStorageRaw(
				FSeinMovementComponent::StaticStruct());
		const ISeinComponentStorage* NavStorage =
			World.GetComponentStorageRaw(
				FSeinNavigationComponent::StaticStruct());
		const ISeinComponentStorage* BrokerStorage =
			World.GetComponentStorageRaw(
				FSeinBrokerMembershipData::StaticStruct());
		USeinLatentActionManager* Actions = World.LatentActionManager;

		// ---- Sweep all commanded units, classify, update per-unit streaks ----
		Rows.Reset();
		int32 NumCommanded = 0, NumMovers = 0, NumPressers = 0, NumPinned = 0;
		int32 PinTurn = 0, PinFloorScale = 0, PinArrival = 0, PinCmd0 = 0;
		FFixedPoint SumCmd, SumAct, SumScale;
		FFixedVector ProblemCentroid = FFixedVector::ZeroVector;
		TMap<FSeinEntityHandle, int32> ProblemByBroker;

		World.GetEntityPool().ForEachEntity([&](
			FSeinEntityHandle Handle,
			const FSeinEntity& Entity)
		{
			const FSeinMovementComponent* Move = MoveStorage
				? static_cast<const FSeinMovementComponent*>(MoveStorage->GetComponentRaw(Handle)) : nullptr;
			if (!Move) return;

			FTraceState& S = States.FindOrAdd(Handle);
			const FFixedVector PosNow = Entity.Transform.GetLocation();
			const bool bContiguous = (S.LastSeenTick == Tick - 1);

			if (!Move->bHasTarget)
			{
				// Order ended since last tick (arrival or cancel — the [ARRIVE] event
				// lines discriminate). Count it toward the running episode.
				if (S.bHadTarget && bInEpisode) { ++EpisodeOrdersEnded; }
				S.bHadTarget = false;
				S.PinStreak = 0; S.PressStreak = 0; S.OrphanStreak = 0;
				S.PrevPos = PosNow; S.LastSeenTick = Tick;
				return;
			}

			++NumCommanded;

			// Three-way motion attribution (planar).
			const FFixedPoint CmdSpeed = Move->Velocity.Size();
			FFixedPoint ActSpeed = CmdSpeed;   // first sighting: neutral (no delta yet)
			FFixedPoint CollSpeed = FFixedPoint::Zero;
			if (bContiguous)
			{
				FFixedVector ActualDelta = PosNow - S.PrevPos;
				ActualDelta.Z = FFixedPoint::Zero;
				ActSpeed = ActualDelta.Size() / DeltaTime;
				const FFixedVector CollDelta(
					ActualDelta.X - Move->Velocity.X * DeltaTime,
					ActualDelta.Y - Move->Velocity.Y * DeltaTime,
					FFixedPoint::Zero);
				CollSpeed = CollDelta.Size() / DeltaTime;
			}

			// Facing-vs-goal alignment (proxy for the mode-policy pivot gate) + goal range.
			FFixedVector ToGoal = Move->TargetLocation - PosNow;
			ToGoal.Z = FFixedPoint::Zero;
			const FFixedPoint GoalDist = ToGoal.Size();
			FFixedPoint AlignDot = FFixedPoint::One;
			if (GoalDist > FFixedPoint::Epsilon)
			{
				const FFixedVector Fwd = Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
				AlignDot = (Fwd.X * ToGoal.X + Fwd.Y * ToGoal.Y) / GoalDist;
			}

			const FSeinNavigationComponent* NavComp = NavStorage
				? static_cast<const FSeinNavigationComponent*>(NavStorage->GetComponentRaw(Handle)) : nullptr;
			const FFixedPoint Accept = (NavComp && NavComp->AcceptanceRadius > FFixedPoint::Zero)
				? NavComp->AcceptanceRadius : FSeinNavigationComponent::DefaultArrivalAcceptance();

			// Classify. PINNED = the movement side commanded ~nothing (the avoidance
			// kernel's own pinned classifier); PRESSER = commanded plenty, body went
			// nowhere (collision ate it) — the population no velocity-gated consumer
			// can see; MOVER = the rest.
			enum : uint8 { CMover, CPresser, CPinned };
			uint8 Class = CMover;
			const TCHAR* Cause = TEXT("-");
			if (CmdSpeed <= PinFloor)
			{
				Class = CPinned; ++NumPinned;
				if (GoalDist <= Accept * FFixedPoint::FromInt(3))      { Cause = TEXT("arr");   ++PinArrival; }
				else if (AlignDot < FFixedPoint::FromInt(1) / FFixedPoint::FromInt(2))
				                                                       { Cause = TEXT("turn");  ++PinTurn; }
				else if (S.LastLiveScale <= FFixedPoint::FromInt(6) / FFixedPoint::FromInt(100))
				                                                       { Cause = TEXT("floor"); ++PinFloorScale; }
				else                                                   { Cause = TEXT("cmd0");  ++PinCmd0; }
				++S.PinStreak; S.PressStreak = 0;
			}
			else
			{
				// Latch live avoidance output while un-pinned — the kernel CLEARS the
				// output every pinned tick, so at pin time this latch is the only
				// record of what braking/steer looked like on the way in.
				S.LastLiveScale = Move->AvoidanceOutput.SpeedScale;
				S.LastLiveSteer = Move->AvoidanceOutput.SteerDir.Size();
				if (bContiguous && CmdSpeed > FFixedPoint::FromInt(50)
					&& ActSpeed * FFixedPoint::FromInt(4) < CmdSpeed)
				{
					Class = CPresser; ++NumPressers;
					Cause = TEXT("press");
					++S.PressStreak; S.PinStreak = 0;
				}
				else
				{
					++NumMovers;
					S.PinStreak = 0; S.PressStreak = 0;
				}
			}

			SumCmd = SumCmd + CmdSpeed;
			SumAct = SumAct + ActSpeed;
			SumScale = SumScale + Move->AvoidanceOutput.SpeedScale;

			// Orphan detector: commanded with NO live latent action → nothing will ever
			// tick it again (the driver skips bHasTarget units). The h=28 signature.
			if (Actions && !Actions->HasActiveActionForEntity(Handle))
			{
				++S.OrphanStreak;
				if (S.OrphanStreak == 30 || (S.OrphanStreak % 300) == 0)
				{
					UE_LOG(LogSeinMoveTrace, Verbose,
						TEXT("[ORPHAN] t=%d h=%d:%d bHasTarget=1 liveAction=0 streak=%d cmd=%.0f goal=%.0f"),
						Tick, Handle.Index, Handle.Generation, S.OrphanStreak,
						CmdSpeed.ToFloat(), GoalDist.ToFloat());
				}
			}
			else { S.OrphanStreak = 0; }

			if (Class != CMover)
			{
				const FSeinBrokerMembershipData* Broker = BrokerStorage
					? static_cast<const FSeinBrokerMembershipData*>(BrokerStorage->GetComponentRaw(Handle)) : nullptr;
				FProblemRow Row;
				Row.Handle = Handle;
				Row.BrokerHandle = Broker ? Broker->CurrentBrokerHandle : FSeinEntityHandle();
				Row.CohesionId = Broker ? Broker->CohesionGroupId : 0;
				Row.Pos = PosNow;
				Row.CmdSpeed = CmdSpeed; Row.ActSpeed = ActSpeed; Row.CollSpeed = CollSpeed;
				Row.Scale = Move->AvoidanceOutput.SpeedScale;
				Row.LastLiveScale = S.LastLiveScale; Row.LastLiveSteer = S.LastLiveSteer;
				Row.AlignDot = AlignDot; Row.GoalDist = GoalDist;
				Row.Streak = (Class == CPinned) ? S.PinStreak : S.PressStreak;
				Row.Cause = Cause;
				Row.MoveClass = &Move->MovementClass;
				Rows.Add(Row);
				ProblemCentroid = ProblemCentroid + PosNow;
				ProblemByBroker.FindOrAdd(Row.BrokerHandle) += 1;
			}

			S.bHadTarget = true;
			S.PrevPos = PosNow;
			S.LastSeenTick = Tick;
		});

		// ---- Episode lifecycle ----
		const int32 Problems = Rows.Num();
		if (!bInEpisode)
		{
			if (Problems >= 8)
			{
				bInEpisode = true;
				EpisodeStartTick = Tick;
				EpisodeQuietTicks = 0;
				EpisodeOrdersEnded = 0;
				EpisodePeakPinned = NumPinned; EpisodePeakPressers = NumPressers;
				const FFixedPoint InvN = FFixedPoint::One / FFixedPoint::FromInt(Problems);
				UE_LOG(LogSeinMoveTrace, Verbose,
					TEXT("[EP] start t=%d problems=%d (pin=%d press=%d) centroid=(%.0f,%.0f)"),
					Tick, Problems, NumPinned, NumPressers,
					(ProblemCentroid.X * InvN).ToFloat(), (ProblemCentroid.Y * InvN).ToFloat());
			}
		}
		else
		{
			EpisodePeakPinned   = FMath::Max(EpisodePeakPinned, NumPinned);
			EpisodePeakPressers = FMath::Max(EpisodePeakPressers, NumPressers);
			if (Problems < 3) { ++EpisodeQuietTicks; } else { EpisodeQuietTicks = 0; }
			if (EpisodeQuietTicks >= 30)
			{
				UE_LOG(LogSeinMoveTrace, Verbose,
					TEXT("[EP] end t=%d durTicks=%d peakPin=%d peakPress=%d ordersEnded=%d"),
					Tick, Tick - EpisodeStartTick, EpisodePeakPinned, EpisodePeakPressers,
					EpisodeOrdersEnded);
				ResetEpisode();
			}
			else if (((Tick - EpisodeStartTick) % 15) == 0)
			{
				// ~0.5s aggregate row. Broker split: top two problem-unit brokers,
				// sorted for stable output.
				TArray<TPair<FSeinEntityHandle, int32>> Brokers;
				for (const TPair<FSeinEntityHandle, int32>& P : ProblemByBroker) { Brokers.Add(P); }
				Brokers.Sort([](const TPair<FSeinEntityHandle, int32>& A, const TPair<FSeinEntityHandle, int32>& B)
					{ return A.Value != B.Value ? A.Value > B.Value : A.Key.Index < B.Key.Index; });
				FString BrokerStr;
				for (int32 i = 0; i < FMath::Min(2, Brokers.Num()); ++i)
				{
					BrokerStr += FString::Printf(TEXT("%s%d:%d=%d"), i ? TEXT(",") : TEXT(""),
						Brokers[i].Key.Index, Brokers[i].Key.Generation, Brokers[i].Value);
				}
				const FFixedPoint InvC = NumCommanded > 0
					? FFixedPoint::One / FFixedPoint::FromInt(NumCommanded) : FFixedPoint::Zero;
				UE_LOG(LogSeinMoveTrace, Verbose,
					TEXT("[EP] agg t=%d cmd=%d mov=%d press=%d pin=%d{turn=%d floor=%d arr=%d cmd0=%d} meanCmd=%.0f meanAct=%.0f meanScale=%.2f brokers=%s ended=%d"),
					Tick, NumCommanded, NumMovers, NumPressers, NumPinned,
					PinTurn, PinFloorScale, PinArrival, PinCmd0,
					(SumCmd * InvC).ToFloat(), (SumAct * InvC).ToFloat(), (SumScale * InvC).ToFloat(),
					*BrokerStr, EpisodeOrdersEnded);

				// Deep-sample the three worst offenders (longest stuck streaks).
				Rows.Sort([](const FProblemRow& A, const FProblemRow& B)
					{ return A.Streak != B.Streak ? A.Streak > B.Streak : A.Handle.Index < B.Handle.Index; });
				for (int32 i = 0; i < FMath::Min(3, Rows.Num()); ++i)
				{
					const FProblemRow& R = Rows[i];
					UE_LOG(LogSeinMoveTrace, Verbose,
						TEXT("[UNIT] t=%d h=%d:%d cls=%s cause=%s streak=%d cmd=%.0f act=%.0f coll=%.0f scale=%.2f lastLive=%.2f/%.2f algn=%.2f goal=%.0f grp=%d:%d coh=%lld"),
						Tick, R.Handle.Index, R.Handle.Generation,
						*R.MoveClass->GetAssetName(), R.Cause, R.Streak,
						R.CmdSpeed.ToFloat(), R.ActSpeed.ToFloat(), R.CollSpeed.ToFloat(),
						R.Scale.ToFloat(), R.LastLiveScale.ToFloat(), R.LastLiveSteer.ToFloat(),
						R.AlignDot.ToFloat(), R.GoalDist.ToFloat(),
						R.BrokerHandle.Index, R.BrokerHandle.Generation, R.CohesionId);
				}
			}
		}

		// Cheap stale-entry sweep (dead entities, despawns) once every ~10s.
		if ((Tick % 300) == 0)
		{
			for (auto It = States.CreateIterator(); It; ++It)
			{
				if (It->Value.LastSeenTick < Tick - 300) { It.RemoveCurrent(); }
			}
		}
#endif // !UE_BUILD_SHIPPING
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.movement.trace")),
			2u,
			ESeinTickPhase::FinalObservation,
			SeinSystemPriority::MovementTrace);
	}

private:
	struct FTraceState
	{
		FFixedVector PrevPos = FFixedVector::ZeroVector;
		int32 LastSeenTick = -1;
		int32 PinStreak = 0;
		int32 PressStreak = 0;
		int32 OrphanStreak = 0;
		bool bHadTarget = false;
		/** Avoidance output latched on the last tick the unit commanded above the pin
		 *  floor — the kernel clears the live output every pinned tick, so this is the
		 *  only record of the scale/steer that carried the unit INTO the pin. */
		FFixedPoint LastLiveScale = FFixedPoint::One;
		FFixedPoint LastLiveSteer;
	};

	struct FProblemRow
	{
		FSeinEntityHandle Handle;
		FSeinEntityHandle BrokerHandle;
		int64 CohesionId = 0;
		FFixedVector Pos;
		FFixedPoint CmdSpeed, ActSpeed, CollSpeed, Scale, LastLiveScale, LastLiveSteer, AlignDot, GoalDist;
		int32 Streak = 0;
		const TCHAR* Cause = TEXT("-");
		const FSoftClassPath* MoveClass = nullptr;
	};

	void ResetEpisode()
	{
		bInEpisode = false;
		EpisodeStartTick = 0;
		EpisodeQuietTicks = 0;
		EpisodeOrdersEnded = 0;
		EpisodePeakPinned = 0;
		EpisodePeakPressers = 0;
	}

	TMap<FSeinEntityHandle, FTraceState> States;
	TArray<FProblemRow> Rows;   // per-tick scratch, reused

	bool bInEpisode = false;
	int32 EpisodeStartTick = 0;
	int32 EpisodeQuietTicks = 0;
	int32 EpisodeOrdersEnded = 0;
	int32 EpisodePeakPinned = 0;
	int32 EpisodePeakPressers = 0;
};
