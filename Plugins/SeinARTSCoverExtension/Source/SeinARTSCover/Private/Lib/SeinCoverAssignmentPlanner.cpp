/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAssignmentPlanner.cpp
 */

#include "Lib/SeinCoverAssignmentPlanner.h"

#include "Lib/SeinCoverGeometry.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinCoverGameplayTags.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SeinCoverAssignmentLocal
{
	struct FLexCost
	{
		int32 Unmatched = 0;
		int32 WrongSide = 0;
		int64 Distance = 0;

		friend FLexCost operator+(const FLexCost& A, const FLexCost& B)
		{
			return {
				A.Unmatched + B.Unmatched,
				A.WrongSide + B.WrongSide,
				A.Distance + B.Distance };
		}

		friend FLexCost operator-(const FLexCost& A, const FLexCost& B)
		{
			return {
				A.Unmatched - B.Unmatched,
				A.WrongSide - B.WrongSide,
				A.Distance - B.Distance };
		}

		FLexCost& operator+=(const FLexCost& Other)
		{
			*this = *this + Other;
			return *this;
		}

		FLexCost& operator-=(const FLexCost& Other)
		{
			*this = *this - Other;
			return *this;
		}

		friend bool operator<(const FLexCost& A, const FLexCost& B)
		{
			if (A.Unmatched != B.Unmatched)
			{
				return A.Unmatched < B.Unmatched;
			}
			if (A.WrongSide != B.WrongSide)
			{
				return A.WrongSide < B.WrongSide;
			}
			return A.Distance < B.Distance;
		}
	};

	static bool TryAddNonnegative(int64 A, int64 B, int64& Out)
	{
		if (A < 0 || B < 0 || A > MAX_int64 - B) return false;
		Out = A + B;
		return true;
	}

	static bool TrySubtract(int64 A, int64 B, int64& Out)
	{
		if ((B > 0 && A < MIN_int64 + B)
			|| (B < 0 && A > MAX_int64 + B))
		{
			return false;
		}
		Out = A - B;
		return true;
	}

	static bool TrySquareFixedRaw(int64 RawValue, int64& OutRawSquare)
	{
		uint64 High = 0;
		uint64 Low = 0;
#if defined(_MSC_VER)
		int64 SignedHigh = 0;
		const int64 SignedLow = _mul128(
			RawValue, RawValue, &SignedHigh);
		High = static_cast<uint64>(SignedHigh);
		Low = static_cast<uint64>(SignedLow);
#elif defined(__GNUC__) || defined(__clang__)
		const unsigned __int128 Product =
			static_cast<unsigned __int128>(
				static_cast<__int128>(RawValue)
				* static_cast<__int128>(RawValue));
		High = static_cast<uint64>(Product >> 64);
		Low = static_cast<uint64>(Product);
#else
#error "Platform does not support 128-bit multiplication"
#endif
		if (High > 0x7FFFFFFFULL) return false;
		const uint64 Shifted = (High << 32) | (Low >> 32);
		if (Shifted > static_cast<uint64>(MAX_int64)) return false;
		OutRawSquare = static_cast<int64>(Shifted);
		return true;
	}

	struct FEligibleEdge
	{
		int32 MemberLocalIndex = INDEX_NONE;
		int32 SlotIndex = INDEX_NONE;
		int64 RawDistance = 0;
		bool bPreferredSide = false;
	};

	static bool TryDistanceSquared(
		const FFixedVector& A,
		const FFixedVector& B,
		int64& OutRawDistance)
	{
		const int64 AValues[] = {
			static_cast<int64>(A.X),
			static_cast<int64>(A.Y),
			static_cast<int64>(A.Z) };
		const int64 BValues[] = {
			static_cast<int64>(B.X),
			static_cast<int64>(B.Y),
			static_cast<int64>(B.Z) };
		int64 Total = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			int64 Delta = 0;
			int64 Square = 0;
			if (!TrySubtract(AValues[Axis], BValues[Axis], Delta)
				|| !TrySquareFixedRaw(Delta, Square)
				|| !TryAddNonnegative(Total, Square, Total))
			{
				return false;
			}
		}
		OutRawDistance = Total;
		return true;
	}
}

void FSeinCoverAssignmentPlan::Apply(
	TArray<FFixedVector>& InOutPositions,
	const TArray<FSeinCoverSlotCandidate>& Slots) const
{
	for (const FSeinCoverSlotAssignment& Assignment : Assignments)
	{
		if (!InOutPositions.IsValidIndex(Assignment.MemberIndex)
			|| !Slots.IsValidIndex(Assignment.SlotCandidateIndex))
		{
			continue;
		}
		InOutPositions[Assignment.MemberIndex] =
			Slots[Assignment.SlotCandidateIndex].WorldPosition;
	}
}

FSeinCoverAssignmentPlan FSeinCoverAssignmentPlanner::Solve(
	const TArray<FFixedVector>& DesiredPositions,
	const TArray<int32>& MemberIndices,
	const TArray<FSeinCoverSlotCandidate>& Slots,
	TConstArrayView<int32> PreferredSlotIndices,
	FFixedPoint SnapRadius)
{
	using namespace SeinCoverAssignmentLocal;
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Cover_Assignment_Solve);

	FSeinCoverAssignmentPlan Plan;
	if (DesiredPositions.IsEmpty() || MemberIndices.IsEmpty()
		|| Slots.IsEmpty() || SnapRadius <= FFixedPoint::Zero)
	{
		return Plan;
	}

	TArray<int32> StableMemberIndices;
	StableMemberIndices.Reserve(FMath::Min(
		MemberIndices.Num(), DesiredPositions.Num()));
	TBitArray<> SeenMembers(false, DesiredPositions.Num());
	for (const int32 MemberIndex : MemberIndices)
	{
		if (!SeenMembers.IsValidIndex(MemberIndex)
			|| SeenMembers[MemberIndex])
		{
			continue;
		}
		SeenMembers[MemberIndex] = true;
		StableMemberIndices.Add(MemberIndex);
	}
	Plan.EligibleMemberCount = StableMemberIndices.Num();
	if (StableMemberIndices.IsEmpty()) return Plan;

	int64 RawSnapRadiusSquared = 0;
	if (!TrySquareFixedRaw(
		static_cast<int64>(SnapRadius), RawSnapRadiusSquared))
	{
		return Plan;
	}

	TBitArray<> PreferredSlots(false, Slots.Num());
	for (int32 SlotIndex : PreferredSlotIndices)
	{
		if (PreferredSlots.IsValidIndex(SlotIndex))
		{
			PreferredSlots[SlotIndex] = true;
		}
	}

	TArray<FEligibleEdge> EligibleEdges;
	int64 MaxRawDistance = 0;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Cover_Assignment_BuildEligibleEdges);
		for (int32 MemberLocalIndex = 0;
			MemberLocalIndex < StableMemberIndices.Num(); ++MemberLocalIndex)
		{
			const int32 MemberIndex = StableMemberIndices[MemberLocalIndex];

			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				int64 RawDistance = 0;
				if (!TryDistanceSquared(
						DesiredPositions[MemberIndex],
						Slots[SlotIndex].WorldPosition,
						RawDistance)
					|| RawDistance > RawSnapRadiusSquared)
				{
					continue;
				}

				FEligibleEdge& Edge = EligibleEdges.Emplace_GetRef();
				Edge.MemberLocalIndex = MemberLocalIndex;
				Edge.SlotIndex = SlotIndex;
				Edge.RawDistance = RawDistance;
				Edge.bPreferredSide = PreferredSlots[SlotIndex];
				MaxRawDistance = FMath::Max(MaxRawDistance, RawDistance);
			}
		}
	}
	if (EligibleEdges.IsEmpty()) return Plan;

	const int32 MemberCount = StableMemberIndices.Num();
	const int32 SlotCount = Slots.Num();
	if (SlotCount > MAX_int32 - MemberCount) return Plan;
	const int32 ColumnCount = SlotCount + MemberCount;
	const int64 CellCount =
		static_cast<int64>(MemberCount) * ColumnCount;
	if (CellCount > MAX_int32) return Plan;
	if (MaxRawDistance > 0
		&& MemberCount > MAX_int64 / 8 / MaxRawDistance)
	{
		return Plan;
	}

	// Rectangular Hungarian assignment. One private dummy column per member
	// guarantees a feasible assignment. Tuple costs preserve the three exact
	// objectives without scalarization or distance precision loss. An
	// ineligible real edge costs two unmatched units, so it cannot beat a dummy.
	TArray<FLexCost> Costs;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Cover_Assignment_BuildCostMatrix);
		Costs.Init({ 2, 0, 0 }, static_cast<int32>(CellCount));
		for (int32 MemberLocalIndex = 0;
			MemberLocalIndex < MemberCount; ++MemberLocalIndex)
		{
			for (int32 ColumnIndex = SlotCount;
				ColumnIndex < ColumnCount; ++ColumnIndex)
			{
				Costs[MemberLocalIndex * ColumnCount + ColumnIndex] =
					{ 1, 0, 0 };
			}
		}
		for (const FEligibleEdge& Edge : EligibleEdges)
		{
			Costs[Edge.MemberLocalIndex * ColumnCount + Edge.SlotIndex] =
				{ 0, Edge.bPreferredSide ? 0 : 1, Edge.RawDistance };
		}
	}

	TArray<FLexCost> RowPotentials;
	TArray<FLexCost> ColumnPotentials;
	TArray<int32> ColumnOwners;
	TArray<int32> PreviousColumns;
	RowPotentials.SetNumZeroed(MemberCount + 1);
	ColumnPotentials.SetNumZeroed(ColumnCount + 1);
	ColumnOwners.Init(0, ColumnCount + 1);
	PreviousColumns.Init(0, ColumnCount + 1);
	TArray<FLexCost> MinimumValues;
	MinimumValues.SetNumUninitialized(ColumnCount + 1);
	TBitArray<> HasMinimum(false, ColumnCount + 1);
	TBitArray<> UsedColumns(false, ColumnCount + 1);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Cover_Assignment_Hungarian);
		for (int32 Row = 1; Row <= MemberCount; ++Row)
		{
			ColumnOwners[0] = Row;
			int32 CurrentColumn = 0;
			HasMinimum.Init(false, ColumnCount + 1);
			UsedColumns.Init(false, ColumnCount + 1);

			do
			{
				UsedColumns[CurrentColumn] = true;
				const int32 CurrentRow = ColumnOwners[CurrentColumn];
				FLexCost Delta;
				bool bHasDelta = false;
				int32 NextColumn = 0;
				for (int32 Column = 1; Column <= ColumnCount; ++Column)
				{
					if (UsedColumns[Column]) continue;
					const FLexCost ReducedCost =
						Costs[(CurrentRow - 1) * ColumnCount + Column - 1]
						- RowPotentials[CurrentRow]
						- ColumnPotentials[Column];
					if (!HasMinimum[Column]
						|| ReducedCost < MinimumValues[Column])
					{
						HasMinimum[Column] = true;
						MinimumValues[Column] = ReducedCost;
						PreviousColumns[Column] = CurrentColumn;
					}
					if (!bHasDelta || MinimumValues[Column] < Delta)
					{
						bHasDelta = true;
						Delta = MinimumValues[Column];
						NextColumn = Column;
					}
				}
				check(bHasDelta && NextColumn != 0);
				for (int32 Column = 0; Column <= ColumnCount; ++Column)
				{
					if (UsedColumns[Column])
					{
						RowPotentials[ColumnOwners[Column]] += Delta;
						ColumnPotentials[Column] -= Delta;
					}
					else if (HasMinimum[Column])
					{
						MinimumValues[Column] -= Delta;
					}
				}
				CurrentColumn = NextColumn;
			}
			while (ColumnOwners[CurrentColumn] != 0);

			do
			{
				const int32 PreviousColumn = PreviousColumns[CurrentColumn];
				ColumnOwners[CurrentColumn] = ColumnOwners[PreviousColumn];
				CurrentColumn = PreviousColumn;
			}
			while (CurrentColumn != 0);
		}
	}

	TArray<int32> MatchedSlotByMember;
	MatchedSlotByMember.Init(INDEX_NONE, MemberCount);
	for (int32 Column = 1; Column <= SlotCount; ++Column)
	{
		const int32 OwnerRow = ColumnOwners[Column];
		if (OwnerRow <= 0) continue;
		const FLexCost MatchCost =
			Costs[(OwnerRow - 1) * ColumnCount + Column - 1];
		if (MatchCost.Unmatched == 0)
		{
			MatchedSlotByMember[OwnerRow - 1] = Column - 1;
		}
	}

	for (int32 MemberLocalIndex = 0;
		MemberLocalIndex < MemberCount; ++MemberLocalIndex)
	{
		const int32 SlotIndex = MatchedSlotByMember[MemberLocalIndex];
		if (!Slots.IsValidIndex(SlotIndex)) continue;

		FSeinCoverSlotAssignment& Assignment =
			Plan.Assignments.Emplace_GetRef();
		Assignment.MemberIndex = StableMemberIndices[MemberLocalIndex];
		Assignment.SlotCandidateIndex = SlotIndex;
		int64 RawDistance = 0;
		const bool bDistanceValid = TryDistanceSquared(
			DesiredPositions[Assignment.MemberIndex],
			Slots[SlotIndex].WorldPosition,
			RawDistance);
		check(bDistanceValid);
		Assignment.DistanceSquared = FFixedPoint(RawDistance);
		Assignment.bPreferredSide = PreferredSlots[SlotIndex];
		if (Assignment.bPreferredSide)
		{
			++Plan.PreferredAssignmentCount;
		}
	}
	return Plan;
}

FSeinCoverAssignmentPlan FSeinCoverAssignmentPlanner::PlanForMembers(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const TArray<FFixedVector>& DesiredPositions,
	const TArray<FSeinCoverSlotCandidate>& Slots,
	FFixedVector TargetLocation,
	FFixedPoint SnapRadius)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Cover_Assignment_PlanForMembers);
	FSeinCoverAssignmentPlan EmptyPlan;
	if (!World) return EmptyPlan;

	TArray<int32> EligibleMemberIndices;
	EligibleMemberIndices.Reserve(FMath::Min(
		Members.Num(), DesiredPositions.Num()));
	for (int32 MemberIndex = 0;
		MemberIndex < Members.Num()
			&& MemberIndex < DesiredPositions.Num(); ++MemberIndex)
	{
		if (World->HasTag(
			Members[MemberIndex], SeinCoverTags::Cover_UsesCover))
		{
			EligibleMemberIndices.Add(MemberIndex);
		}
	}

	TArray<int32> PreferredSlotIndices;
	TArray<int32> WrongSideSlotIndices;
	SeinCoverGeometry::PartitionSlotsByCursorSide(
		World,
		Slots,
		TargetLocation,
		PreferredSlotIndices,
		WrongSideSlotIndices);

	return Solve(
		DesiredPositions,
		EligibleMemberIndices,
		Slots,
		PreferredSlotIndices,
		SnapRadius);
}
