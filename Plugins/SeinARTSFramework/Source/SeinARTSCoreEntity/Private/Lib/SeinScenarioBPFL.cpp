/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinScenarioBPFL.cpp
 */

#include "Lib/SeinScenarioBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Events/SeinVisualEvent.h"
#include "Engine/World.h"

USeinWorldSubsystem* USeinScenarioBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinScenarioBPFL::SeinSetSimPaused(const UObject* WorldContextObject, bool bPaused)
{
	if (USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject))
	{
		Sub->SetSimPaused(bPaused);
	}
}

bool USeinScenarioBPFL::SeinIsSimPaused(const UObject* WorldContextObject)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	return Sub && Sub->IsSimulationPaused();
}

void USeinScenarioBPFL::SeinPlayPreRenderedCinematic(
	const UObject* WorldContextObject,
	FGameplayTag CinematicID,
	bool bBlocksSim,
	ESeinCinematicSkipMode SkipMode,
	int32 VoteThreshold)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	if (bBlocksSim)
	{
		Sub->SetSimPaused(true);
	}
	Sub->EnqueueVisualEvent(FSeinVisualEvent::MakePlayPreRenderedCinematicEvent(
		CinematicID, static_cast<int32>(SkipMode), VoteThreshold));
}

void USeinScenarioBPFL::SeinEndCinematic(const UObject* WorldContextObject, FGameplayTag CinematicID, bool bResumeSimIfPaused)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return;
	Sub->EnqueueVisualEvent(FSeinVisualEvent::MakeEndCinematicEvent(CinematicID));
	if (bResumeSimIfPaused && Sub->IsSimulationPaused())
	{
		Sub->SetSimPaused(false);
	}
}
