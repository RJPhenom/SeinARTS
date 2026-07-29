/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinSimContext.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Sim context thread-local storage definition.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Core/SeinSimContext.h"

static thread_local const USeinWorldSubsystem* GSeinSimContextWorld = nullptr;

bool SeinIsInSimContext() { return GSeinSimContextWorld != nullptr; }
bool SeinIsInSimContext(const USeinWorldSubsystem* World)
{
	return World && GSeinSimContextWorld == World;
}

FSeinSimContextScope::FSeinSimContextScope(
	const USeinWorldSubsystem& World)
	: PreviousWorld(GSeinSimContextWorld)
{
	GSeinSimContextWorld = &World;
}

FSeinSimContextScope::~FSeinSimContextScope()
{
	GSeinSimContextWorld = PreviousWorld;
}
