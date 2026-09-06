// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#include "Debug/SeinNavPathDebug.h"
#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSMovement::NavDebug
{
	void CollectActions(const USeinWorldSubsystem& Sim, TArray<const USeinMoveToAction*>& Out)
	{
		Out.Reset();
		const USeinLatentActionManager* Manager = Sim.GetLatentActionManager();
		if (!Manager) return;
		for (const auto& Entry : Manager->GetActiveActions())
		{
			const USeinMoveToAction* Action = Cast<USeinMoveToAction>(Entry.Get());
			if (!IsValid(Action) || Action->bCompleted || Action->bCancelled || Action->bFailed
				|| !IsValid(Action->OwningAbility)
				|| Action->OwningAbility->GetWorld() != Sim.GetWorld()
				|| !Sim.GetEntity(Action->OwnerEntity)) continue;
			Out.Add(Action);
		}
	}

	namespace
	{
		FVector ProjectLeg(const FVector& From, const FVector& To, const FVector& Position)
		{
			const FVector Delta = To - From;
			const double LengthSq = Delta.SizeSquared();
			const double T = LengthSq > UE_SMALL_NUMBER
				? FMath::Clamp(FVector::DotProduct(Position - From, Delta) / LengthSq, 0.0, 1.0) : 1.0;
			return From + Delta * T;
		}

		void AddSegment(FRoute& Out, const FSeinPathSegment& Segment,
			const FVector* Position)
		{
			const FVector From = Segment.From.ToVector();
			const FVector To = Segment.To.ToVector();
			const double Radius = Segment.Radius.ToFloat();
			const double Sweep = Segment.SweepAngle.ToFloat();
			if (Segment.Type != ESeinPathSegmentType::Arc || Radius <= 0.0 || FMath::Abs(Sweep) < UE_SMALL_NUMBER)
			{
				Out.Lines.Add({Position ? ProjectLeg(From, To, *Position) : From, To, Segment.bReverse});
				return;
			}
			const FVector Center = Segment.Center.ToVector();
			const double Start = FMath::Atan2(From.Y - Center.Y, From.X - Center.X);
			const double Sign = Sweep < 0.0 ? -1.0 : 1.0;
			const double Extent = FMath::Abs(Sweep);
			double Progress = 0.0;
			if (Position)
			{
				const double Angle = FMath::Atan2(Position->Y - Center.Y, Position->X - Center.X);
				Progress = FMath::Fmod(Sign * (Angle - Start) + UE_TWO_PI, UE_TWO_PI);
				// Outside the signed arc: choose its nearer endpoint, not a later leg.
				if (Progress > Extent)
					Progress = FVector::DistSquared(*Position, From) <= FVector::DistSquared(*Position, To) ? 0.0 : Extent;
			}
			// Render-only tessellation, <= 2 cm sagitta until the explicit 512 edge cap.
			const double Step = FMath::Max(0.01, 2.0 * FMath::Acos(FMath::Clamp(1.0 - 2.0 / Radius, -1.0, 1.0)));
			const int32 Count = FMath::Clamp(FMath::CeilToInt((Extent - Progress) / Step), 1, 512);
			auto At = [&](double Distance)
			{
				if (Distance <= 0.0) return From;
				if (Distance >= Extent) return To;
				const double Angle = Start + Sign * Distance;
				return FVector(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle),
					FMath::Lerp(From.Z, To.Z, Distance / Extent));
			};
			FVector Previous = At(Progress);
			for (int32 I = 1; I <= Count; ++I)
			{
				const FVector Next = I == Count ? To : At(FMath::Lerp(Progress, Extent, double(I) / Count));
				Out.Lines.Add({Previous, Next, Segment.bReverse});
				Previous = Next;
			}
		}
	}

	FRoute BuildRoute(const FSeinPath& Path, int32 WaypointIndex, int32 TypedSegmentIndex,
		const FVector& Origin, const FVector& RenderPosition)
	{
		FRoute Out;
		if (!Path.bIsValid || Path.Waypoints.IsEmpty()) return Out;
		Out.Endpoint = Path.Waypoints.Last().ToVector();
		Out.bPartial = Path.bIsPartial;
		if (TypedSegmentIndex != INDEX_NONE)
		{
			if (!Path.Segments.IsValidIndex(TypedSegmentIndex)) return Out;
			Out.bSegmentDriven = true;
			Out.Target = Path.Segments[TypedSegmentIndex].To.ToVector();
			for (int32 I = TypedSegmentIndex; I < Path.Segments.Num(); ++I)
				AddSegment(Out, Path.Segments[I], I == TypedSegmentIndex ? &RenderPosition : nullptr);
		}
		else
		{
			if (!Path.Waypoints.IsValidIndex(WaypointIndex)) return Out;
			Out.Target = Path.Waypoints[WaypointIndex].ToVector();
			const FVector Previous = WaypointIndex > 0 ? Path.Waypoints[WaypointIndex - 1].ToVector() : Origin;
			Out.Lines.Add({ProjectLeg(Previous, Out.Target, RenderPosition), Out.Target, false});
			for (int32 I = WaypointIndex; I + 1 < Path.Waypoints.Num(); ++I)
				Out.Lines.Add({Path.Waypoints[I].ToVector(), Path.Waypoints[I + 1].ToVector(), false});
		}
		Out.bValid = true;
		return Out;
	}
}
