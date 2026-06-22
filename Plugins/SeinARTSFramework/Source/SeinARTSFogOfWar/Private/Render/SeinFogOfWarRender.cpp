/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarRender.cpp
 */

#include "Render/SeinFogOfWarRender.h"

#include "Components/PostProcessComponent.h"
#include "Engine/BlendableInterface.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
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

#if !UE_BUILD_SHIPPING
// Dev-only convenience for driving the vision-layer switch from the console.
// The shipping switch path is SetActiveVisionLayer / CycleVisionLayer (BP/input).
static void SeinVisionLayerConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
	if (!World) return;
	const bool bCycle = (Args.Num() == 0);
	const int32 Layer = bCycle ? -1 : FCString::Atoi(*Args[0]);
	int32 Count = 0;
	for (TActorIterator<ASeinFogOfWarRender> It(World); It; ++It)
	{
		if (bCycle) { It->CycleVisionLayer(); }
		else        { It->SetActiveVisionLayer(Layer); }
		++Count;
	}
	UE_LOG(LogSeinFogOfWar, Display, TEXT("[Sein.Vision.Layer] %s on %d fog render actor(s)."),
		bCycle ? TEXT("cycled") : *FString::Printf(TEXT("set layer %d"), Layer), Count);
}

static FAutoConsoleCommandWithWorldAndArgs GSeinVisionLayerCmd(
	TEXT("Sein.Vision.Layer"),
	TEXT("Fog render: set the local active vision layer (-1=Normal, 0..5=custom slot). No arg = cycle Normal + enabled layers."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SeinVisionLayerConsoleCommand));
#endif

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

	// Six fixed vision-layer slots (index N → EVNNNNNN bit 2+N) so the mapping is
	// stable + visible in the details panel. All disabled by default — Normal view
	// + the three core tiers work with none enabled.
	VisionLayerPostProcessMaterials.SetNum(6);
}

void ASeinFogOfWarRender::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Mat = FogPostProcessMaterial.LoadSynchronous())
	{
		FogMID = UMaterialInstanceDynamic::Create(Mat, this);
	}
	else
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("[FogRender] %s has no FogPostProcessMaterial assigned — fog overlay is inert."),
			*GetName());
	}

	RefreshBlendables();   // [ (style if any), fog ]

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

void ASeinFogOfWarRender::SetActiveVisionLayer(int32 LayerIndex)
{
	if (LayerIndex < -1) LayerIndex = -1;

	// Switching to a custom slot is only allowed if that slot is live.
	if (LayerIndex >= 0 &&
		(!VisionLayerPostProcessMaterials.IsValidIndex(LayerIndex) ||
		 !VisionLayerPostProcessMaterials[LayerIndex].bEnabled))
	{
		UE_LOG(LogSeinFogOfWar, Verbose,
			TEXT("[FogRender] Ignoring switch to vision layer %d (out of range or disabled)."), LayerIndex);
		return;
	}

	if (LayerIndex == ActiveVisionLayer) return;

	ActiveVisionLayer = LayerIndex;
	UpdateStyleBlendable();   // swap the full-screen style for the new layer
	RebuildTexture();         // re-bake the fog reveal from the new layer's bit
}

void ASeinFogOfWarRender::CycleVisionLayer()
{
	// Ordered cycle: Normal (-1), then each ENABLED custom slot in index order.
	TArray<int32, TInlineAllocator<7>> Order;
	Order.Add(-1);
	for (int32 i = 0; i < VisionLayerPostProcessMaterials.Num(); ++i)
	{
		if (VisionLayerPostProcessMaterials[i].bEnabled) { Order.Add(i); }
	}

	int32 Cur = Order.IndexOfByKey(ActiveVisionLayer);
	if (Cur == INDEX_NONE) { Cur = 0; }   // active slot got disabled → restart at Normal
	SetActiveVisionLayer(Order[(Cur + 1) % Order.Num()]);
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

uint8 ASeinFogOfWarRender::ComputeVisibleMask() const
{
	if (ActiveVisionLayer < 0 || ActiveVisionLayer >= 6)
	{
		return SEIN_FOW_BIT_NORMAL;   // Normal view → the V bit
	}

	uint8 Mask = static_cast<uint8>(1u << (2 + ActiveVisionLayer));   // the layer's N-bit
	if (VisionLayerPostProcessMaterials.IsValidIndex(ActiveVisionLayer) &&
		VisionLayerPostProcessMaterials[ActiveVisionLayer].bCombineWithNormalVision)
	{
		Mask |= SEIN_FOW_BIT_NORMAL;   // additive: this layer OR normal vision
	}
	return Mask;
}

void ASeinFogOfWarRender::UpdateStyleBlendable()
{
	ActiveStyleMaterial = nullptr;
	if (VisionLayerPostProcessMaterials.IsValidIndex(ActiveVisionLayer))
	{
		const FSeinVisionLayerView& View = VisionLayerPostProcessMaterials[ActiveVisionLayer];
		if (View.bEnabled)
		{
			ActiveStyleMaterial = View.PostProcessMaterial.LoadSynchronous();
		}
	}
	RefreshBlendables();
}

void ASeinFogOfWarRender::RefreshBlendables()
{
	if (!PostProcess) return;

	// Order = application order: style composites UNDER, fog ON TOP. Fog must win
	// so unexplored stays hidden — otherwise a thermal recolor would re-light
	// blacked-out terrain and leak the map.
	TArray<FWeightedBlendable>& Blendables = PostProcess->Settings.WeightedBlendables.Array;
	Blendables.Reset();
	if (ActiveStyleMaterial) { Blendables.Add(FWeightedBlendable(1.0f, ActiveStyleMaterial.Get())); }
	if (FogMID)              { Blendables.Add(FWeightedBlendable(1.0f, FogMID.Get())); }
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
	FogTexture->Filter = (SmoothingStrength > 0.0f) ? TF_Bilinear : TF_Nearest;
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

	const uint8 VisibleMask = ComputeVisibleMask();
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FColor C = TintForCell(Cells[i], VisibleMask).ToFColor(/*bSRGB*/ false);
		const int32 o = i * 4;
		PixelBuffer[o + 0] = C.B;
		PixelBuffer[o + 1] = C.G;
		PixelBuffer[o + 2] = C.R;
		PixelBuffer[o + 3] = C.A;
	}

	if (SmoothingStrength > 0.0f)
	{
		BlurPixelBuffer(SmoothingStrength);
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

void ASeinFogOfWarRender::BlurPixelBuffer(float Radius)
{
	if (Radius <= 0.0f || TexWidth <= 0 || TexHeight <= 0) return;
	if (PixelBuffer.Num() != TexWidth * TexHeight * 4) return;

	const int32 W = TexWidth;
	const int32 H = TexHeight;
	const int32 R = FMath::FloorToInt(Radius);
	const float Frac = Radius - static_cast<float>(R);          // weight of the outer (R+1) tap
	const float Norm = 1.0f / ((2 * R + 1) + 2.0f * Frac);

	TArray<uint8> Tmp;
	Tmp.SetNumUninitialized(W * H * 4);

	// Horizontal pass: PixelBuffer -> Tmp (clamp at edges).
	for (int32 y = 0; y < H; ++y)
	{
		const int32 Row = y * W;
		for (int32 x = 0; x < W; ++x)
		{
			float Acc[4] = { 0.f, 0.f, 0.f, 0.f };
			for (int32 k = -R; k <= R; ++k)
			{
				const int32 SI = (Row + FMath::Clamp(x + k, 0, W - 1)) * 4;
				for (int32 c = 0; c < 4; ++c) { Acc[c] += PixelBuffer[SI + c]; }
			}
			if (Frac > 0.0f)
			{
				const int32 LI = (Row + FMath::Clamp(x - (R + 1), 0, W - 1)) * 4;
				const int32 RI = (Row + FMath::Clamp(x + (R + 1), 0, W - 1)) * 4;
				for (int32 c = 0; c < 4; ++c) { Acc[c] += Frac * (PixelBuffer[LI + c] + PixelBuffer[RI + c]); }
			}
			const int32 DI = (Row + x) * 4;
			for (int32 c = 0; c < 4; ++c) { Tmp[DI + c] = (uint8)FMath::Clamp(FMath::RoundToInt(Acc[c] * Norm), 0, 255); }
		}
	}

	// Vertical pass: Tmp -> PixelBuffer.
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			float Acc[4] = { 0.f, 0.f, 0.f, 0.f };
			for (int32 k = -R; k <= R; ++k)
			{
				const int32 SI = (FMath::Clamp(y + k, 0, H - 1) * W + x) * 4;
				for (int32 c = 0; c < 4; ++c) { Acc[c] += Tmp[SI + c]; }
			}
			if (Frac > 0.0f)
			{
				const int32 TI = (FMath::Clamp(y - (R + 1), 0, H - 1) * W + x) * 4;
				const int32 BI = (FMath::Clamp(y + (R + 1), 0, H - 1) * W + x) * 4;
				for (int32 c = 0; c < 4; ++c) { Acc[c] += Frac * (Tmp[TI + c] + Tmp[BI + c]); }
			}
			const int32 DI = (y * W + x) * 4;
			for (int32 c = 0; c < 4; ++c) { PixelBuffer[DI + c] = (uint8)FMath::Clamp(FMath::RoundToInt(Acc[c] * Norm), 0, 255); }
		}
	}
}

FLinearColor ASeinFogOfWarRender::TintForCell(uint8 Bits, uint8 VisibleMask) const
{
	const bool bExplored = (Bits & SEIN_FOW_BIT_EXPLORED) != 0;
	const bool bVisible  = (Bits & VisibleMask) != 0;

	float A;
	if (bVisible)        { A = 0.0f; }                 // visible on the active layer → clear
	else if (bExplored)  { A = ExploredOpacity; }      // explored memory → dimmed
	else                 { A = UnexploredOpacity; }    // never seen → opaque

	return FLinearColor(UnexploredColor.R, UnexploredColor.G, UnexploredColor.B,
		FMath::Clamp(A, 0.0f, 1.0f));
}

void ASeinFogOfWarRender::HandleFogMutated()
{
	RebuildTexture();
}

#if WITH_EDITOR
void ASeinFogOfWarRender::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	// Live-tune during PIE: drop the texture so a Smoothing-Strength (filter) change
	// takes effect, re-apply the active layer's style, then re-bake tints from the
	// current grid. Outside a running world ResolveFog() returns null and the
	// rebuild is a no-op.
	FogTexture = nullptr;
	TexWidth = TexHeight = 0;
	UpdateStyleBlendable();
	RebuildTexture();
}
#endif
