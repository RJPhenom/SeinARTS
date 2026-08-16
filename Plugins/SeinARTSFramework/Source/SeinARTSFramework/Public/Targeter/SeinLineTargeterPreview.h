/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinLineTargeterPreview.h
 * @author       RJ Macklem
 * @created      15 Aug 2026
 * @brief        Default line/corridor targeter preview.
 *
 *          Renders the segment being authored as a ground-projected rectangle
 *          decal stretched from the anchor (drag start or previous polyline
 *          vertex) to the cursor, sized to the spec's Width (a thin default
 *          when Width is zero) and tinted by validity. Committed segments
 *          (multi-segment captures) accumulate as their own decals until the
 *          session submits or cancels.
 *
 *          Same material contract as the point preview: assign the decal
 *          material on the inherited SegmentDecal component in a BP subclass;
 *          the framework wraps it in a dynamic instance and pushes a
 *          `TintColor` parameter for validity feedback.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Targeter/SeinTargeterPreview.h"
#include "SeinLineTargeterPreview.generated.h"

class UDecalComponent;

UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinLineTargeterPreview : public ASeinTargeterPreview
{
	GENERATED_BODY()

public:
	ASeinLineTargeterPreview();

	/** Rendered half-width when the spec's Width is zero (pure line). World
	 *  units — kept thin so a widthless line reads as a line, not a lane. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	float DefaultLineHalfWidth = 15.0f;

	/** Decal projection depth above + below the ground (slope tolerance). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	float DecalHeight = 200.0f;

protected:
	/** Decal drawing the segment currently being authored. Hidden until an
	 *  anchor exists (drag in progress / polyline vertex planted). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	TObjectPtr<UDecalComponent> SegmentDecal;

	/** Decals for segments already captured this session (multi-segment
	 *  specs). Cleared with the actor when the session ends. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> CommittedSegmentDecals;

	virtual void BeginPlay() override;
	virtual void OnPreviewUpdated_Implementation() override;
	virtual void OnPointCaptured_Implementation(
		const FVector& StartWorld, const FVector& EndWorld) override;

private:
	/** Shape one decal into a ground rectangle spanning Start→End. */
	void LayoutSegmentDecal(UDecalComponent& Decal,
		const FVector& StartWorld, const FVector& EndWorld) const;

	/** Half-width from the spec's Width (or the default when zero). */
	float ResolveHalfWidth() const;
};
