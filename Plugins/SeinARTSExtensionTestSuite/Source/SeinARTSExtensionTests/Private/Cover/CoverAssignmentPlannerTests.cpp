#include "CQTest.h"

#include "Lib/SeinCoverAssignmentPlanner.h"

namespace UE::SeinARTSExtensionTests
{
	namespace CoverAssignmentTestLocal
	{
		struct FObjective
		{
			int32 MatchCount = 0;
			int32 WrongSideCount = 0;
			int64 RawDistance = 0;
		};

		static FFixedVector Position(int32 X)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::Zero,
				FFixedPoint::Zero);
		}

		static TArray<FSeinCoverSlotCandidate> SlotsAt(
			std::initializer_list<int32> XValues)
		{
			TArray<FSeinCoverSlotCandidate> Result;
			for (int32 X : XValues)
			{
				FSeinCoverSlotCandidate& Slot = Result.Emplace_GetRef();
				Slot.WorldPosition = Position(X);
			}
			return Result;
		}

		static bool IsBetter(const FObjective& A, const FObjective& B)
		{
			if (A.MatchCount != B.MatchCount)
			{
				return A.MatchCount > B.MatchCount;
			}
			if (A.WrongSideCount != B.WrongSideCount)
			{
				return A.WrongSideCount < B.WrongSideCount;
			}
			return A.RawDistance < B.RawDistance;
		}

		static FObjective ObjectiveForPlan(
			const FSeinCoverAssignmentPlan& Plan)
		{
			FObjective Result;
			Result.MatchCount = Plan.Num();
			Result.WrongSideCount = Plan.WrongSideAssignmentCount();
			for (const FSeinCoverSlotAssignment& Assignment : Plan.Assignments)
			{
				Result.RawDistance +=
					static_cast<int64>(Assignment.DistanceSquared);
			}
			return Result;
		}

		static void SearchExhaustive(
			const TArray<FFixedVector>& Members,
			const TArray<FSeinCoverSlotCandidate>& Slots,
			const TBitArray<>& Preferred,
			FFixedPoint RadiusSquared,
			int32 MemberIndex,
			TBitArray<>& UsedSlots,
			const FObjective& Current,
			FObjective& InOutBest)
		{
			if (MemberIndex >= Members.Num())
			{
				if (IsBetter(Current, InOutBest)) InOutBest = Current;
				return;
			}

			SearchExhaustive(
				Members,
				Slots,
				Preferred,
				RadiusSquared,
				MemberIndex + 1,
				UsedSlots,
				Current,
				InOutBest);

			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				if (UsedSlots[SlotIndex]) continue;
				const FFixedPoint DistanceSquared = FFixedVector::DistSquared(
					Members[MemberIndex], Slots[SlotIndex].WorldPosition);
				if (DistanceSquared < FFixedPoint::Zero
					|| DistanceSquared > RadiusSquared)
				{
					continue;
				}

				FObjective Candidate = Current;
				++Candidate.MatchCount;
				if (!Preferred[SlotIndex]) ++Candidate.WrongSideCount;
				Candidate.RawDistance += static_cast<int64>(DistanceSquared);
				UsedSlots[SlotIndex] = true;
				SearchExhaustive(
					Members,
					Slots,
					Preferred,
					RadiusSquared,
					MemberIndex + 1,
					UsedSlots,
					Candidate,
					InOutBest);
				UsedSlots[SlotIndex] = false;
			}
		}

		static FObjective ExhaustiveObjective(
			const TArray<FFixedVector>& Members,
			const TArray<FSeinCoverSlotCandidate>& Slots,
			TConstArrayView<int32> PreferredIndices,
			FFixedPoint Radius)
		{
			TBitArray<> Preferred(false, Slots.Num());
			for (int32 Index : PreferredIndices)
			{
				if (Preferred.IsValidIndex(Index)) Preferred[Index] = true;
			}
			TBitArray<> UsedSlots(false, Slots.Num());
			FObjective Best;
			SearchExhaustive(
				Members,
				Slots,
				Preferred,
				Radius * Radius,
				0,
				UsedSlots,
				{},
				Best);
			return Best;
		}
	}

	TEST(MaximumCardinalityAvoidsGreedyStarvation,
		"SeinARTS.Unit.Cover.Assignment")
	{
		using namespace CoverAssignmentTestLocal;
		const TArray<FFixedVector> Members{Position(0), Position(10)};
		const TArray<int32> MemberIndices{0, 1};
		const TArray<FSeinCoverSlotCandidate> Slots = SlotsAt({4, -5});
		const TArray<int32> Preferred{0, 1};

		const FSeinCoverAssignmentPlan Plan =
			FSeinCoverAssignmentPlanner::Solve(
				Members,
				MemberIndices,
				Slots,
				Preferred,
				FFixedPoint::FromInt(6));

		ASSERT_THAT(AreEqual(2, Plan.Num()));
		ASSERT_THAT(AreEqual(1, Plan.Assignments[0].SlotCandidateIndex));
		ASSERT_THAT(AreEqual(0, Plan.Assignments[1].SlotCandidateIndex));
	}

	TEST(AssignmentObjectivePrefersSideThenDistance,
		"SeinARTS.Unit.Cover.Assignment")
	{
		using namespace CoverAssignmentTestLocal;
		const TArray<FFixedVector> Members{Position(0), Position(10)};
		const TArray<int32> MemberIndices{0, 1};
		const TArray<FSeinCoverSlotCandidate> Slots = SlotsAt({2, 8, 1});
		const TArray<int32> Preferred{0, 1};

		const FSeinCoverAssignmentPlan Plan =
			FSeinCoverAssignmentPlanner::Solve(
				Members,
				MemberIndices,
				Slots,
				Preferred,
				FFixedPoint::FromInt(20));

		ASSERT_THAT(AreEqual(2, Plan.Num()));
		ASSERT_THAT(AreEqual(2, Plan.PreferredAssignmentCount));
		ASSERT_THAT(AreEqual(0, Plan.Assignments[0].SlotCandidateIndex));
		ASSERT_THAT(AreEqual(1, Plan.Assignments[1].SlotCandidateIndex));
	}

	TEST(EqualCostAssignmentsUseStableInputOrder,
		"SeinARTS.Unit.Cover.Assignment")
	{
		using namespace CoverAssignmentTestLocal;
		const TArray<FFixedVector> Members{Position(0), Position(0)};
		const TArray<int32> MemberIndices{0, 1};
		const TArray<FSeinCoverSlotCandidate> Slots = SlotsAt({1, 1});
		const TArray<int32> Preferred{0, 1};

		const FSeinCoverAssignmentPlan Plan =
			FSeinCoverAssignmentPlanner::Solve(
				Members,
				MemberIndices,
				Slots,
				Preferred,
				FFixedPoint::FromInt(2));

		ASSERT_THAT(AreEqual(2, Plan.Num()));
		ASSERT_THAT(AreEqual(0, Plan.Assignments[0].MemberIndex));
		ASSERT_THAT(AreEqual(0, Plan.Assignments[0].SlotCandidateIndex));
		ASSERT_THAT(AreEqual(1, Plan.Assignments[1].MemberIndex));
		ASSERT_THAT(AreEqual(1, Plan.Assignments[1].SlotCandidateIndex));

		const TArray<int32> RepeatedAndInvalidMembers{0, 0, INDEX_NONE, 1, 7};
		const FSeinCoverAssignmentPlan SanitizedPlan =
			FSeinCoverAssignmentPlanner::Solve(
				Members,
				RepeatedAndInvalidMembers,
				Slots,
				Preferred,
				FFixedPoint::FromInt(2));
		ASSERT_THAT(AreEqual(2, SanitizedPlan.EligibleMemberCount));
		ASSERT_THAT(AreEqual(2, SanitizedPlan.Num()));
		ASSERT_THAT(AreEqual(0, SanitizedPlan.Assignments[0].MemberIndex));
		ASSERT_THAT(AreEqual(1, SanitizedPlan.Assignments[1].MemberIndex));

		const FSeinCoverAssignmentPlan OverflowPlan =
			FSeinCoverAssignmentPlanner::Solve(
				Members,
				MemberIndices,
				Slots,
				Preferred,
				FFixedPoint::FromInt(65536));
		ASSERT_THAT(AreEqual(0, OverflowPlan.Num()));

		const TArray<FFixedVector> RerouteMembers{
			Position(-1), Position(1) };
		const TArray<FSeinCoverSlotCandidate> RerouteSlots =
			SlotsAt({ 0, -2, 2 });
		const TArray<int32> ReroutePreferred{ 0, 1, 2 };
		const FSeinCoverAssignmentPlan ReroutePlan =
			FSeinCoverAssignmentPlanner::Solve(
				RerouteMembers,
				MemberIndices,
				RerouteSlots,
				ReroutePreferred,
				FFixedPoint::FromInt(1));
		ASSERT_THAT(AreEqual(2, ReroutePlan.Num()));
		ASSERT_THAT(AreEqual(1, ReroutePlan.Assignments[0].SlotCandidateIndex));
		ASSERT_THAT(AreEqual(0, ReroutePlan.Assignments[1].SlotCandidateIndex));
	}

	TEST(OptimizedAssignmentMatchesExhaustiveSmallMatrices,
		"SeinARTS.Unit.Cover.Assignment")
	{
		using namespace CoverAssignmentTestLocal;
		for (int32 MemberCount = 1; MemberCount <= 4; ++MemberCount)
		{
			for (int32 SlotCount = 1; SlotCount <= 4; ++SlotCount)
			{
				for (int32 Scenario = 0; Scenario < 8; ++Scenario)
				{
					TArray<FFixedVector> Members;
					TArray<int32> MemberIndices;
					for (int32 Index = 0; Index < MemberCount; ++Index)
					{
						Members.Add(Position(
							Index * 7 + ((Scenario + Index) % 3)));
						MemberIndices.Add(Index);
					}

					TArray<FSeinCoverSlotCandidate> Slots;
					TArray<int32> Preferred;
					for (int32 Index = 0; Index < SlotCount; ++Index)
					{
						FSeinCoverSlotCandidate& Slot = Slots.Emplace_GetRef();
						Slot.WorldPosition = Position(
							Index * 6 + ((Scenario * 3 + Index) % 5) - 2);
						if (((Index + Scenario) % 2) == 0) Preferred.Add(Index);
					}
					const FFixedPoint Radius =
						FFixedPoint::FromInt(5 + (Scenario % 4) * 3);

					const FSeinCoverAssignmentPlan Plan =
						FSeinCoverAssignmentPlanner::Solve(
							Members,
							MemberIndices,
							Slots,
							Preferred,
							Radius);
					const FObjective Actual = ObjectiveForPlan(Plan);
					const FObjective Expected = ExhaustiveObjective(
						Members, Slots, Preferred, Radius);

					ASSERT_THAT(AreEqual(Expected.MatchCount, Actual.MatchCount));
					ASSERT_THAT(AreEqual(
						Expected.WrongSideCount, Actual.WrongSideCount));
					ASSERT_THAT(AreEqual(
						Expected.RawDistance, Actual.RawDistance));
				}
			}
		}
	}

	TEST(DenseSelectionAssignmentHasBoundedCost,
		"SeinARTS.Perf.Cover.Assignment")
	{
		using namespace CoverAssignmentTestLocal;
		constexpr int32 SelectionSize = 128;
		constexpr int32 TimedIterations = 5;
		TArray<FFixedVector> Members;
		TArray<int32> MemberIndices;
		TArray<FSeinCoverSlotCandidate> Slots;
		TArray<int32> Preferred;
		Members.Reserve(SelectionSize);
		MemberIndices.Reserve(SelectionSize);
		Slots.Reserve(SelectionSize);
		Preferred.Reserve(SelectionSize / 2);
		for (int32 Index = 0; Index < SelectionSize; ++Index)
		{
			Members.Add(Position(Index * 2));
			MemberIndices.Add(Index);
			FSeinCoverSlotCandidate& Slot = Slots.Emplace_GetRef();
			Slot.WorldPosition = Position((SelectionSize - Index) * 2 + 1);
			if ((Index % 2) == 0) Preferred.Add(Index);
		}

		const FFixedPoint Radius = FFixedPoint::FromInt(500);
		const FSeinCoverAssignmentPlan Warmup =
			FSeinCoverAssignmentPlanner::Solve(
				Members, MemberIndices, Slots, Preferred, Radius);
		ASSERT_THAT(AreEqual(SelectionSize, Warmup.Num()));

		const double StartedAt = FPlatformTime::Seconds();
		for (int32 Iteration = 0; Iteration < TimedIterations; ++Iteration)
		{
			const FSeinCoverAssignmentPlan Plan =
				FSeinCoverAssignmentPlanner::Solve(
					Members, MemberIndices, Slots, Preferred, Radius);
			ASSERT_THAT(AreEqual(SelectionSize, Plan.Num()));
		}
		const double AverageMilliseconds =
			(FPlatformTime::Seconds() - StartedAt) * 1000.0
			/ TimedIterations;
		UE_LOG(LogTemp, Display,
			TEXT("Dense 128x128 cover assignment average: %.3f ms"),
			AverageMilliseconds);
		ASSERT_THAT(IsTrue(AverageMilliseconds < 25.0));
	}
}
