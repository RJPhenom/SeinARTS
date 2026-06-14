/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarRender.cpp
 */

#include "Render/SeinFogOfWarRender.h"

#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHI.h"

#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "SeinARTSFogOfWarLog.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

const FName ASeinFogOfWarRender::P_FogTexture(TEXT("FogTexture"));
const FName ASeinFogOfWarRender::P_WorldMin(TEXT("FogWorldMin"));
const FName ASeinFogOfWarRender::P_WorldSize(TEXT("FogWorldSize"));

ASeinFogOfWarRender::ASeinFogOfWarRender()
{
	// Poll only for observer changes (e.g. local PC late-sets its SeinPlayerID,
	// or an observer cam switches players). Vision CONTENT changes arrive via
	// OnFogOfWarMutated, not the tick — so a coarse interval is plenty.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	RootComponent = PostProcess;
	PostProcess->bUnbound = true;     // tint the whole view; actor position is irrelevant
	PostProcess->BlendWeight = 1.0f;

	// Pre-seed the 6 custom-layer slots (index N → EVNNNNNN bit 2+N) so the
	// mapping is stable + visible in the details panel. Disabled by default —
	// the three core tiers work with none enabled.
	CustomLayers.SetNum(6);
	static const FLinearColor Seed[6] = {
		FLinearColor(0.20f, 0.40f, 1.00f), FLinearColor(0.55f, 0.30f, 1.00f),
		FLinearColor(0.70f, 0.20f, 0.90f), FLinearColor(0.85f, 0.45f, 1.00f),
		FLinearColor(1.00f, 0.25f, 0.85f), FLinearColor(1.00f, 0.55f, 0.80f)
	};
	for (int32 i = 0; i < 6; ++i) { CustomLayers[i].Color = Seed[i]; }
}

void ASeinFogOfWarRender::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Mat = FogPostProcessMaterial.LoadSynchronous())
	{
		FogMID = UMaterialInstanceDynamic::Create(Mat, this);
		PostProcess->AddOrUpdateBlendable(FogMID, 1.0f);
	}
	else
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("[FogRender] %s has no FogPostProcessMaterial assigned — fog overlay is inert."),
			*GetName());
	}

	if (USeinFogOfWar* Fog = ResolveFog())
	{
		SubscribedFog = Fog;
		FogMutatedHandle = Fog->OnFogOfWarMutated.AddUObject(this, &ASeinFogOfWarRender::HandleFogMutated);
	}

	ResolveObserver();
	RebuildTexture();
}

void ASeinFogOfWarRender::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USeinFogOfWar* Fog = SubscribedFog.Get())
	{
		Fog->OnFogOfWarMutated.Remove(FogMutatedHandle);
	}
	SubscribedFog = nullptr;
	FogMutatedHandle.Reset();
	Super::EndPlay(Reason);
}

void ASeinFogOfWarRender::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Rebuild on observer change, and keep retrying until the first texture is
	// built (covers the case where the fog grid wasn't ready at BeginPlay and
	// no mutate has fired yet).
	const bool bObserverChanged = ResolveObserver();
	if (bObserverChanged || !FogTexture)
	{
		RebuildTexture();
	}
}

void ASeinFogOfWarRender::RefreshFogRender()
{
	ResolveObserver();
	RebuildTexture();
}

USeinFogOfWar* ASeinFogOfWarRender::ResolveFog() const
{
	return USeinFogOfWarSubsystem::GetFogOfWarForWorld(this);
}

bool ASeinFogOfWarRender::ResolveObserver()
{
	const FSeinPlayerID Obs = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(GetWorld());
	if (bObserverResolved && Obs == CachedObserver)
	{
		return false;
	}
	CachedObserver = Obs;
	bObserverResolved = true;
	return true;
}

void ASeinFogOfWarRender::EnsureTexture(int32 W, int32 H)
{
	if (W <= 0 || H <= 0) return;
	if (FogTexture && TexWidth == W && TexHeight == H) return;

	TexWidth = W;
	TexHeight = H;
	PixelBuffer.Reset();
	PixelBuffer.AddZeroed(W * H * 4);

	FogTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!FogTexture) return;

	FogTexture->SRGB = false;            // tint bytes are linear; no gamma decode
	FogTexture->Filter = bSmoothEdges ? TF_Bilinear : TF_Nearest;
	FogTexture->MipGenSettings = TMGS_NoMipmaps;
	FogTexture->NeverStream = true;
	FogTexture->UpdateResource();

	if (FogMID)
	{
		FogMID->SetTextureParameterValue(P_FogTexture, FogTexture);
	}
}

void ASeinFogOfWarRender::RebuildTexture()
{
	USeinFogOfWar* Fog = ResolveFog();
	if (!Fog) return;

	TArray<uint8> Cells;
	FFixedVector Origin = FFixedVector::ZeroVector;
	FFixedPoint CellSize = FFixedPoint::Zero;
	int32 W = 0, H = 0;
	if (!Fog->GetObserverGrid(CachedObserver, Cells, Origin, CellSize, W, H)) return;
	if (W <= 0 || H <= 0 || Cells.Num() != W * H) return;

	EnsureTexture(W, H);
	if (!FogTexture || PixelBuffer.Num() != W * H * 4) return;

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FColor C = TintForCell(Cells[i]).ToFColor(/*bSRGB*/ false);
		const int32 o = i * 4;
		PixelBuffer[o + 0] = C.B;
		PixelBuffer[o + 1] = C.G;
		PixelBuffer[o + 2] = C.R;
		PixelBuffer[o + 3] = C.A;
	}
	UploadPixels();

	if (FogMID)
	{
		const float OX = Origin.X.ToFloat();
		const float OY = Origin.Y.ToFloat();
		const float Csz = CellSize.ToFloat();
		FogMID->SetVectorParameterValue(P_WorldMin, FLinearColor(OX, OY, 0.f, 0.f));
		FogMID->SetVectorParameterValue(P_WorldSize, FLinearColor(W * Csz, H * Csz, 0.f, 0.f));
	}
}

void ASeinFogOfWarRender::UploadPixels()
{
	if (!FogTexture || TexWidth <= 0 || TexHeight <= 0) return;
	if (PixelBuffer.Num() != TexWidth * TexHeight * 4) return;

	const int32 NumBytes = TexWidth * TexHeight * 4;
	uint8* Src = static_cast<uint8*>(FMemory::Malloc(NumBytes));
	FMemory::Memcpy(Src, PixelBuffer.GetData(), NumBytes);

	// Region + source buffer are owned by the render command and freed in its
	// cleanup callback (UpdateTextureRegions runs async on the render thread).
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, TexWidth, TexHeight);
	FogTexture->UpdateTextureRegions(
		0, 1, Region,
		static_cast<uint32>(TexWidth * 4),
		static_cast<uint32>(4),
		Src,
		[](uint8* InSrc, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(InSrc);
			delete InRegion;
		});
}

FLinearColor ASeinFogOfWarRender::TintForCell(uint8 Bits) const
{
	const bool bExplored = (Bits & SEIN_FOW_BIT_EXPLORED) != 0;
	const bool bVisible  = (Bits & SEIN_FOW_BIT_NORMAL) != 0;

	// Base tier (the three requested tiers):
	FLinearColor Rgb = UnexploredColor;
	float A;
	if (bVisible)        { A = 0.0f; }                 // currently visible → clear
	else if (bExplored)  { A = ExploredOpacity; }      // explored memory → dimmed
	else                 { A = UnexploredOpacity; }    // never seen → opaque

	// Custom layers (slot N → EVNNNNNN bit 2+N): blend the layer tint over the
	// base, and make sure it shows even over visible terrain.
	const int32 Count = FMath::Min(CustomLayers.Num(), 6);
	for (int32 i = 0; i < Count; ++i)
	{
		const uint8 Bit = static_cast<uint8>(1u << (2 + i));
		if ((Bits & Bit) != 0 && CustomLayers[i].bEnabled)
		{
			const float Op = FMath::Clamp(CustomLayers[i].Opacity, 0.0f, 1.0f);
			Rgb = FMath::Lerp(Rgb, CustomLayers[i].Color, Op);
			A = FMath::Max(A, Op);
		}
	}

	return FLinearColor(Rgb.R, Rgb.G, Rgb.B, FMath::Clamp(A, 0.0f, 1.0f));
}

void ASeinFogOfWarRender::HandleFogMutated()
{
	RebuildTexture();
}

#if WITH_EDITOR
void ASeinFogOfWarRender::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	// Live-tune during PIE: drop the texture so a Smooth-Edges (filter) change
	// takes effect, then re-bake tints from the current grid. Outside a running
	// world ResolveFog() returns null and this is a no-op.
	FogTexture = nullptr;
	TexWidth = TexHeight = 0;
	RebuildTexture();
}
#endif
