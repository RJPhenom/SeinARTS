#include "CQTest.h"
#include "Math/GeometryQueries.h"
#include "Types/Random.h"
#include "Types/Vector2D.h"

namespace UE::SeinARTSTests
{
	TEST(FixedVectorAbsoluteComponentsPreserveFixedPointValues, "SeinARTS.Unit.Core")
	{
		const FFixedVector2D Vector2D(
			-FFixedPoint::FromInt(5),
			FFixedPoint::FromInt(3));
		ASSERT_THAT(IsTrue(
			FFixedVector2D::GetAbsMax(Vector2D) == FFixedPoint::FromInt(5)));
		ASSERT_THAT(IsTrue(
			FFixedVector2D::GetAbsMin(Vector2D) == FFixedPoint::FromInt(3)));

		const FFixedVector Vector3D(
			FFixedPoint::MinValue,
			-FFixedPoint::FromInt(7),
			FFixedPoint::FromInt(2));
		ASSERT_THAT(IsTrue(
			FFixedVector::GetAbsMax(Vector3D) == FFixedPoint::MaxValue));
		ASSERT_THAT(IsTrue(
			FFixedVector::GetAbsMin(Vector3D) == FFixedPoint::FromInt(2)));
	}

	TEST(RandomRotatorConvertsRadianSamplesToDegrees, "SeinARTS.Unit.Core")
	{
		FFixedRandom Expected(0x12345678ULL);
		const FFixedPoint ExpectedPitch = Expected.Pitch() * FFixedPoint::RadToDeg;
		const FFixedPoint ExpectedYaw = Expected.Yaw() * FFixedPoint::RadToDeg;
		const FFixedPoint ExpectedRoll = Expected.Roll() * FFixedPoint::RadToDeg;

		FFixedRandom Actual(0x12345678ULL);
		const FFixedRotator Rotator = Actual.RandomRotator();
		ASSERT_THAT(IsTrue(Rotator.Pitch == ExpectedPitch));
		ASSERT_THAT(IsTrue(Rotator.Yaw == ExpectedYaw));
		ASSERT_THAT(IsTrue(Rotator.Roll == ExpectedRoll));
		ASSERT_THAT(IsTrue(Rotator.Pitch >= FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(Rotator.Pitch <= FFixedPoint::FromInt(360)));
		ASSERT_THAT(IsTrue(Rotator.Yaw >= FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(Rotator.Yaw <= FFixedPoint::FromInt(360)));
		ASSERT_THAT(IsTrue(Rotator.Roll >= FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(Rotator.Roll <= FFixedPoint::FromInt(360)));
	}

	TEST(RandomSpatialDrawOrderIsExplicit, "SeinARTS.Unit.Core")
	{
		const FFixedVector2D RectCentre(
			FFixedPoint::FromInt(10), FFixedPoint::FromInt(20));
		const FFixedVector2D RectExtents(
			FFixedPoint::FromInt(2), FFixedPoint::FromInt(3));
		FFixedRandom ExpectedRect(0xABCDEFULL);
		const FFixedPoint ExpectedRectX = ExpectedRect.Range(
			-RectExtents.X, RectExtents.X);
		const FFixedPoint ExpectedRectY = ExpectedRect.Range(
			-RectExtents.Y, RectExtents.Y);
		FFixedRandom ActualRect(0xABCDEFULL);
		const FFixedVector2D RectPoint = ActualRect.PointInRect(
			RectCentre, RectExtents);
		ASSERT_THAT(IsTrue(RectPoint.X == RectCentre.X + ExpectedRectX));
		ASSERT_THAT(IsTrue(RectPoint.Y == RectCentre.Y + ExpectedRectY));

		const FFixedVector BoxCentre(
			FFixedPoint::FromInt(10),
			FFixedPoint::FromInt(20),
			FFixedPoint::FromInt(30));
		const FFixedVector BoxExtents(
			FFixedPoint::FromInt(2),
			FFixedPoint::FromInt(3),
			FFixedPoint::FromInt(4));
		FFixedRandom ExpectedBox(0xFEDCBAULL);
		const FFixedPoint ExpectedBoxX = ExpectedBox.Range(
			-BoxExtents.X, BoxExtents.X);
		const FFixedPoint ExpectedBoxY = ExpectedBox.Range(
			-BoxExtents.Y, BoxExtents.Y);
		const FFixedPoint ExpectedBoxZ = ExpectedBox.Range(
			-BoxExtents.Z, BoxExtents.Z);
		FFixedRandom ActualBox(0xFEDCBAULL);
		const FFixedVector BoxPoint = ActualBox.PointInBox(BoxCentre, BoxExtents);
		ASSERT_THAT(IsTrue(BoxPoint.X == BoxCentre.X + ExpectedBoxX));
		ASSERT_THAT(IsTrue(BoxPoint.Y == BoxCentre.Y + ExpectedBoxY));
		ASSERT_THAT(IsTrue(BoxPoint.Z == BoxCentre.Z + ExpectedBoxZ));
	}

	TEST(RayBoxIntersectionHasNoArbitraryDistanceCeiling, "SeinARTS.Unit.Core")
	{
		const FFixedRay Ray(
			FFixedVector::ZeroVector,
			FFixedVector::ForwardVector);
		const FFixedBox Box(
			FFixedVector(
				FFixedPoint::FromInt(20000),
				-FFixedPoint::One,
				-FFixedPoint::One),
			FFixedVector(
				FFixedPoint::FromInt(20001),
				FFixedPoint::One,
				FFixedPoint::One));

		FFixedPoint HitDistance = FFixedPoint::Zero;
		ASSERT_THAT(IsTrue(SeinGeometry::RayIntersectsBox(Ray, Box, HitDistance)));
		ASSERT_THAT(IsTrue(HitDistance == FFixedPoint::FromInt(20000)));
	}
}
