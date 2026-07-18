#include "CQTest.h"

#include "Determinism/SeinCollisionDeterminismScenario.h"

namespace
{
	constexpr int32 TraceTicks = 120;

	void LogTrace(const FSeinCollisionDeterminismTrace& Trace)
	{
		for (const FSeinCollisionDeterminismFrame& Frame : Trace.Frames)
		{
			UE_LOG(LogTemp, Display, TEXT("[SeinDeterminismTrace] %s"), *Frame.ToLogPayload());
		}
	}
}

namespace UE::SeinARTSTests
{
	TEST(CollisionStateHashSerialParallelMatches, "SeinARTS.Determinism")
	{
		const FSeinCollisionDeterminismTrace Serial =
			SeinRunCollisionDeterminismScenario(false, TraceTicks);
		if (const FString Error = Serial.Validate(false, TraceTicks); !Error.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Serial trace is invalid: %s"), *Error));
			return;
		}

		const FSeinCollisionDeterminismTrace Parallel =
			SeinRunCollisionDeterminismScenario(true, TraceTicks);
		if (const FString Error = Parallel.Validate(true, TraceTicks); !Error.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Parallel trace is invalid: %s"), *Error));
			return;
		}

		for (int32 Index = 0; Index < TraceTicks; ++Index)
		{
			const FSeinCollisionDeterminismFrame& SerialFrame = Serial.Frames[Index];
			const FSeinCollisionDeterminismFrame& ParallelFrame = Parallel.Frames[Index];
			const bool bPosesMatch = SerialFrame.PoseWords == ParallelFrame.PoseWords;
			const bool bHashesMatch = SerialFrame.StateHash == ParallelFrame.StateHash;
			const bool bDigestsMatch = SerialFrame.PoseDigest == ParallelFrame.PoseDigest;
			if (!bPosesMatch || !bHashesMatch || !bDigestsMatch)
			{
				const TCHAR* MismatchKind = !bPosesMatch
					? TEXT("collision pose mismatch")
					: (!bHashesMatch ? TEXT("StateHash-only mismatch") : TEXT("pose-digest mismatch"));
				AddError(FString::Printf(
					TEXT("Serial/parallel divergence at tick %d: serial state=0x%08X pose=0x%016llX, ")
					TEXT("parallel state=0x%08X pose=0x%016llX (%s)."),
					SerialFrame.Tick,
					SerialFrame.StateHash,
					static_cast<unsigned long long>(SerialFrame.PoseDigest),
					ParallelFrame.StateHash,
					static_cast<unsigned long long>(ParallelFrame.PoseDigest),
					MismatchKind));
				return;
			}
		}
	}

	TEST(SerialCollisionTrace, "SeinARTS.Determinism.Process")
	{
		const FSeinCollisionDeterminismTrace Trace =
			SeinRunCollisionDeterminismScenario(false, TraceTicks);
		if (const FString Error = Trace.Validate(false, TraceTicks); !Error.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Serial process trace is invalid: %s"), *Error));
			return;
		}
		LogTrace(Trace);
	}

	TEST(ParallelCollisionTrace, "SeinARTS.Determinism.Process")
	{
		const FSeinCollisionDeterminismTrace Trace =
			SeinRunCollisionDeterminismScenario(true, TraceTicks);
		if (const FString Error = Trace.Validate(true, TraceTicks); !Error.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Parallel process trace is invalid: %s"), *Error));
			return;
		}
		LogTrace(Trace);
	}
}
