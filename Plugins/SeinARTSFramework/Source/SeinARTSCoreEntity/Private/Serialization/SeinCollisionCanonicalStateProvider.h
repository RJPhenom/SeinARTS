/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionCanonicalStateProvider.h
 * @brief   Module-lifetime registration entry point for the collision
 *          resolver's exact world-binding contract.
 */

#pragma once

#include "Serialization/SeinCanonicalStateRegistry.h"

FSeinCanonicalStateRegistrationHandle
SeinRegisterCollisionCanonicalStateProvider(FString& OutError);
