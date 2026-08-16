/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    LineTargeterSpecTests.cpp
 * @brief   Line targeter spec client-validation contract: segment length
 *          gating is advisory UX that blocks only beyond MaxSegmentLength.
 */

#include "CQTest.h"

#include "Abilities/SeinTargeterSpec.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

namespace UE::SeinARTSTests
{
	namespace LineTargeterSpecTestLocal
	{
		FFixedVector Point(int32 X)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X), FFixedPoint::Zero, FFixedPoint::Zero);
		}
	}

	TEST(LineTargeterSpecLengthGateBlocksOnlyBeyondMax,
		"SeinARTS.Unit.CoreEntity.Targeter")
	{
		using namespace LineTargeterSpecTestLocal;
		USeinLineTargeterSpec* Spec = NewObject<USeinLineTargeterSpec>();
		ASSERT_THAT(IsNotNull(Spec));

		// No aux point yet (no segment authored) — always valid, even with a
		// max configured.
		Spec->MaxSegmentLength = FFixedPoint::FromInt(500);
		ASSERT_THAT(IsTrue(
			Spec->ValidateClient(Point(0), FFixedVector())
				== ESeinTargeterValidity::Valid));

		// Within the limit — valid; beyond it — blocked.
		ASSERT_THAT(IsTrue(
			Spec->ValidateClient(Point(0), Point(400))
				== ESeinTargeterValidity::Valid));
		ASSERT_THAT(IsTrue(
			Spec->ValidateClient(Point(0), Point(501))
				== ESeinTargeterValidity::Blocked));

		// Zero max = unlimited.
		Spec->MaxSegmentLength = FFixedPoint::Zero;
		ASSERT_THAT(IsTrue(
			Spec->ValidateClient(Point(0), Point(100000))
				== ESeinTargeterValidity::Valid));
	}

	TEST(LineTargeterSpecDefaultsSupportBothCaptureModes,
		"SeinARTS.Unit.CoreEntity.Targeter")
	{
		USeinLineTargeterSpec* Spec = NewObject<USeinLineTargeterSpec>();
		ASSERT_THAT(IsNotNull(Spec));
		// Drag is the default gesture; MultiClick is a first-class authored
		// alternative, not a subclass — both modes live on one spec.
		ASSERT_THAT(IsTrue(
			Spec->CaptureMode == ESeinLineTargeterCapture::Drag));
		Spec->CaptureMode = ESeinLineTargeterCapture::MultiClick;
		ASSERT_THAT(IsTrue(
			Spec->CaptureMode == ESeinLineTargeterCapture::MultiClick));
		// Width zero = pure line by default; segment count rides TargetCount.
		ASSERT_THAT(IsTrue(Spec->Width == FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(1, Spec->TargetCount));
	}
}
