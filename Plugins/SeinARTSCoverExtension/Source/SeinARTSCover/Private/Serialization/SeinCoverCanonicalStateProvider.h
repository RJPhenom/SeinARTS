/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverCanonicalStateProvider.h
 * @brief   Module-lifetime registration entry point for the cover
 *          world-binding contributor.
 */

#pragma once

#include "Serialization/SeinCanonicalStateRegistry.h"

FSeinCanonicalStateRegistrationHandle
SeinRegisterCoverCanonicalStateProvider(FString& OutError);
