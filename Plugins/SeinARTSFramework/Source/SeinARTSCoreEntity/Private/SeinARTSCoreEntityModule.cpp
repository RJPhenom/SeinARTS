/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSCoreEntityModule.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Module implementation for SeinARTSCoreEntity.
 *				Registers the module with IMPLEMENT_MODULE and provides
 *				empty startup/shutdown stubs for future initialization.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "SeinARTSCoreEntityModule.h"
#include "SeinARTSCoreEntityLog.h"

// Module-shared log categories. Declared extern in SeinARTSCoreEntityLog.h and
// defined ONCE here — previously each was DEFINE_LOG_CATEGORY_STATIC in several
// .cpp files, which collided whenever the adaptive unity build packed two of
// those files into the same translation unit. Defined unconditionally (UE_LOG
// resolves them in every build config).
DEFINE_LOG_CATEGORY(LogSeinSim);
DEFINE_LOG_CATEGORY(LogSeinBridge);
DEFINE_LOG_CATEGORY(LogSeinBPFL);

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinStateHashCmd, Log, All);

// Per-tick state hash logging toggle. When nonzero, USeinWorldSubsystem
// logs ComputeStateHash() after every sim tick — run two PIE clients with
// this on and diff the logs; any divergence localizes the desync tick.
// Stripped in shipping.
static TAutoConsoleVariable<int32> CVarSeinLogStateHash(
	TEXT("Sein.Sim.StateHash.Log"),
	0,
	TEXT("If nonzero, log the sim state hash each tick. Determinism-verification tool — diff logs across two PIE clients to find the tick where lockstep broke."),
	ECVF_Default);

// One-shot: dump the current state hash on demand. Useful for pause-and-
// compare debugging without spamming logs.
static FAutoConsoleCommandWithWorldAndArgs CmdSeinDumpStateHash(
	TEXT("Sein.Sim.StateHash"),
	TEXT("Log the current USeinWorldSubsystem::ComputeStateHash() value once."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
		{
			if (!World) return;
			if (USeinWorldSubsystem* Sub = World->GetSubsystem<USeinWorldSubsystem>())
			{
				UE_LOG(LogSeinStateHashCmd, Log,
					TEXT("StateHash[tick %d] = 0x%08x"),
					Sub->GetCurrentTick(),
					static_cast<uint32>(Sub->ComputeStateHash()));
			}
		}));
#endif // !UE_BUILD_SHIPPING

IMPLEMENT_MODULE(FSeinARTSCoreEntity, SeinARTSCoreEntity);

void FSeinARTSCoreEntity::StartupModule()
{
}

void FSeinARTSCoreEntity::ShutdownModule()
{
}
