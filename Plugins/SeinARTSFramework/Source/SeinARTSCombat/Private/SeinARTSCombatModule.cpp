/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCombatModule.cpp
 * @brief   Combat module lifecycle. Components, systems, and policy classes
 *          register through the ordinary reflection/subsystem paths; nothing
 *          here mutates global registries that would need paired teardown.
 */

#include "SeinARTSCombatModule.h"

void FSeinARTSCombatModule::StartupModule()
{
}

void FSeinARTSCombatModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSeinARTSCombatModule, SeinARTSCombat)
