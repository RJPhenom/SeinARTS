/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAvoidanceDefaultKernel.h
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
 * @brief        Declares the private deterministic default-avoidance kernel.
 *
 *               The public avoidance UObject remains the selected policy and
 *               authoring surface. This private kernel owns tick execution and
 *               carries no persistent state between calls.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

class USeinAvoidanceDefault;
class USeinWorldSubsystem;

/** Executes one default-avoidance tick without changing the public policy seam. */
class FSeinAvoidanceDefaultKernel final
{
public:
	explicit FSeinAvoidanceDefaultKernel(
		const USeinAvoidanceDefault& InPolicy)
		: Policy(InPolicy)
	{
	}

	void Execute(USeinWorldSubsystem& World) const;

private:
	const USeinAvoidanceDefault& Policy;
};
