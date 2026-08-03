/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementPlusModule.cpp
 * @brief   Module implementation for SeinARTSMovementPlus. Registers stable
 *          simulation-content and exact movement-state coverage descriptors.
 */

#include "SeinARTSMovementPlusModule.h"
#include "Movement/SeinFlightMovement.h"
#include "Movement/SeinHoverMovement.h"
#include "Movement/SeinInfantryMovement.h"
#include "Movement/SeinTrackedVehicleMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_MODULE(FSeinARTSMovementPlusModule, SeinARTSMovementPlus)

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSMovementPlus, Log, All);

namespace
{
	FSeinMovementStateCoverageRegistrationHandle RegisterCoverage(
		const UClass* NativeClass,
		ESeinMovementStateCoverage Coverage,
		FString& OutError)
	{
		FSeinMovementStateCoverageDescriptor Descriptor;
		Descriptor.NativeClass = NativeClass;
		Descriptor.Coverage = Coverage;
		return FSeinMovementStateCoverageRegistry::Register(
			TEXT("SeinARTSMovementPlus"), Descriptor, &OutError);
	}

	void WithdrawCoverage(
		TArray<FSeinMovementStateCoverageRegistrationHandle>& Handles)
	{
		FString Error;
		if (!FSeinMovementStateCoverageRegistry::UnregisterAll(
			Handles, &Error))
		{
			UE_LOG(LogSeinARTSMovementPlus, Error,
				TEXT("Atomic state coverage withdrawal failed: %s"),
				*Error);
		}
	}
}

void FSeinARTSMovementPlusModule::StartupModule()
{
	StateCoverageHandles.Reset();
	SimulationContentRegistrationHandle.Reset();

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.movementplus");
	ContentDescriptor.ContributorRevision = 1;

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSMovementPlus,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	auto AddCoverage = [this](
		const UClass* NativeClass,
		ESeinMovementStateCoverage Coverage)
	{
		FString Error;
		FSeinMovementStateCoverageRegistrationHandle Handle =
			RegisterCoverage(NativeClass, Coverage, Error);
		if (!Handle.IsValid())
		{
			UE_LOG(
				LogSeinARTSMovementPlus,
				Error,
				TEXT("State coverage registration failed for '%s': %s"),
				NativeClass
					? *NativeClass->GetPathName()
					: TEXT("<null>"),
				*Error);
			return;
		}
		StateCoverageHandles.Add(MoveTemp(Handle));
	};

	AddCoverage(
		USeinInfantryMovement::StaticClass(),
		ESeinMovementStateCoverage::Stateless);
	AddCoverage(
		USeinHoverMovement::StaticClass(),
		ESeinMovementStateCoverage::Stateless);
	AddCoverage(
		USeinWheeledVehicleMovement::StaticClass(),
		ESeinMovementStateCoverage::ReflectedComplete);
	AddCoverage(
		USeinTrackedVehicleMovement::StaticClass(),
		ESeinMovementStateCoverage::ReflectedComplete);
	AddCoverage(
		USeinFlightMovement::StaticClass(),
		ESeinMovementStateCoverage::ReflectedComplete);
}

void FSeinARTSMovementPlusModule::PreUnloadCallback()
{
	check(IsInGameThread());

	// Invalidate every live topology and release framework-owned extension
	// state while this DLL is still callable. UObject destruction itself remains
	// owned by UE's GC/reinstancing lifecycle.
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSMovementPlus"),
				TEXT("native movement policies and private state are unloading"));
		}
	}

	for (TObjectIterator<USeinMovementSubsystem> It; It; ++It)
	{
		if (It->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		It->ReleaseNativeClassStateForModuleUnload(
			TEXT("SeinARTSMovementPlus"));
	}

	WithdrawCoverage(StateCoverageHandles);
	SimulationContentRegistrationHandle.Reset();
}

void FSeinARTSMovementPlusModule::ShutdownModule()
{
	WithdrawCoverage(StateCoverageHandles);
	SimulationContentRegistrationHandle.Reset();
}
