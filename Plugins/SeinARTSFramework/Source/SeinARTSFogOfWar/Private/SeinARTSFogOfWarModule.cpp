/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarModule.cpp
 * @brief   Module startup + debug toggle.
 *
 *          Registers `ShowFlags.FogOfWar` (custom show flag — UE doesn't
 *          ship one out of the box) and the `Sein.FogOfWar.Show`
 *          console command that toggles it across all viewports — same UX
 *          as nav's `Sein.Nav.Show` against the built-in
 *          `ShowFlags.Navigation`. Menu-item icon resolves via
 *          `ShowFlagsMenu.FogOfWar` in the SeinARTSEditor style set
 *          (SeinGrayIcon16.svg).
 *
 *          The show flag gates rendering of the fog-of-war debug viz
 *          (scene-proxy backed, lands in the next pass). Non-PIE: whole
 *          grid red. PIE: local player controller's visible + explored cells.
 *
 *          Shipping strip: the custom show flag, console command, and all
 *          helper code are gated on `UE_ENABLE_DEBUG_DRAWING`. In
 *          `UE_BUILD_SHIPPING` StartupModule / ShutdownModule are no-ops.
 *
 *          Editor-side integrations (ASeinFogOfWarVolume details panel +
 *          entity-bridge vision-stamp draw layer) live in the companion
 *          SeinARTSFogOfWarEditor module — separate so they load at
 *          PostEngineInit phase after SeinARTSEditor is ready, mirroring the
 *          SeinARTSCoverEditor pattern. Was previously a `#if WITH_EDITOR`
 *          block here, but the Runtime/Default load phase meant
 *          SeinARTSEditor wasn't loaded yet, and the deferred
 *          OnFEngineLoopInitComplete path proved unreliable. New module
 *          eliminates the load-order race entirely.
 */

#include "SeinARTSFogOfWarModule.h"
#include "SeinARTSFogOfWarLog.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "ShowFlags.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UObject/UObjectIterator.h"
#include "Debug/SeinFogOfWarDebugComponent.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinVisionLayerDefinition.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSFogOfWarModule, SeinARTSFogOfWar)

// Module-shared log categories (declared in SeinARTSFogOfWarLog.h). Defined here —
// once per module — so they register at module load and appear in the Output Log
// filter (see the log header's rationale).
DEFINE_LOG_CATEGORY(LogSeinFogOfWar);
DEFINE_LOG_CATEGORY(LogSeinFogOfWarSubsystem);
DEFINE_LOG_CATEGORY(LogSeinFogOfWarDebug);

// NOTE: the editor-side vision-stamp draw helpers + the entity-bridge
// draw-callback registration that used to live here have moved to the
// dedicated SeinARTSFogOfWarEditor module. See the file header for the
// load-order rationale. The block below (#if UE_ENABLE_DEBUG_DRAWING) is
// runtime-only — it owns the custom show flag + console commands that work
// in both editor and shipping-debug builds.


#if UE_ENABLE_DEBUG_DRAWING

// Custom show flag registration. TCustomShowFlag ctor auto-calls
// FEngineShowFlags::RegisterCustomShowFlag on static init (module-load time,
// which is game-thread for Runtime modules). The show-flag menu queries
// registered custom flags when opened; this is all that's needed for "Fog Of
// War" to appear under the Developer group with the registered icon.
//
// Named namespace so the debug viz component (added in the next pass) can
// query the same flag via FEngineShowFlags::FindIndexByName(TEXT("FogOfWar"))
// without needing a link-time reference to this translation unit.
namespace UE::SeinARTSFogOfWar
{
	// SFG_Normal puts the entry in the top-level "Common Show Flags" section
	// alongside Navigation, Collision, etc. — matches nav's UX and where
	// RTS devs will look. SFG_Developer buries it two submenus deep under
	// All Show Flags → Developer, which is where ZoneGraph-style plugins
	// put theirs but isn't the right fit for a framework feature.
	static TCustomShowFlag<> ShowFogOfWar(
		TEXT("FogOfWar"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSFogOfWar", "ShowFogOfWar", "Fog Of War"));
}

namespace
{
	IConsoleCommand* GShowFogOfWarCmd = nullptr;
	IConsoleCommand* GPlayerPerspectiveCmd = nullptr;
	IConsoleCommand* GLayerPerspectiveCmd = nullptr;

	// Debug observer override. -1 = no override (use local PC). 0..255 = pinned
	// FSeinPlayerID::Value. Lockstep sim is symmetric so every client's
	// USeinFogOfWarDefault holds every player's VisionGroup; this toggle just
	// picks which one the proxy renders on THIS client.
	int32 GDebugObserverOverride = -1;

	// Debug layer override. -1 = no override (use V / Normal bit). 0..7 =
	// pinned EVNNNNNN bit to render through. 0 = E (Explored), 1 = V
	// (Normal), 2..7 = N0..N5 custom layers.
	int32 GDebugLayerOverride = -1;

	/** Set ShowFlags.FogOfWar across all editor + game viewport clients. */
	static void SetFogOfWarShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSFogOfWar::ShowFogOfWar.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport)
		{
			UE::SeinARTSFogOfWar::ShowFogOfWar.SetEnabled(GEngine->GameViewport->EngineShowFlags, bEnable);
		}
	}

	/** True if any viewport currently has the flag on. */
	static bool IsFogOfWarShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && UE::SeinARTSFogOfWar::ShowFogOfWar.IsEnabled(Vp->EngineShowFlags))
				{
					return true;
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport &&
			UE::SeinARTSFogOfWar::ShowFogOfWar.IsEnabled(GEngine->GameViewport->EngineShowFlags))
		{
			return true;
		}
		return false;
	}

	/** Force the fog debug proxies to rebuild so the observer change lands
	 *  immediately instead of on the next stamp tick. */
	static void MarkAllFogDebugProxiesDirty()
	{
		for (TObjectIterator<USeinFogOfWarDebugComponent> It; It; ++It)
		{
			if (IsValid(*It))
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	static void OnLayerPerspectiveCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		if (Args.Num() == 0)
		{
			GDebugLayerOverride = -1;
			UE_LOG(LogTemp, Log, TEXT("Sein.FogOfWar.Show.Layer = V (reset to Normal)"));
			MarkAllFogDebugProxiesDirty();
			return;
		}
		const int32 Parsed = FCString::Atoi(*Args[0]);
		if (Parsed < 0 || Parsed > 7)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Sein.FogOfWar.Show.Layer: expected 0-7 (0=E, 1=V, 2..7=N0..N5), got %s"),
				*Args[0]);
			return;
		}
		GDebugLayerOverride = Parsed;
		static const TCHAR* LayerNames[] = {
			TEXT("E (Explored)"), TEXT("V (Normal)"),
			TEXT("N0"), TEXT("N1"), TEXT("N2"), TEXT("N3"), TEXT("N4"), TEXT("N5")
		};
		UE_LOG(LogTemp, Log,
			TEXT("Sein.FogOfWar.Show.Layer = bit %d (%s). Use no-args to reset to V."),
			Parsed, LayerNames[Parsed]);
		MarkAllFogDebugProxiesDirty();
	}

	static void OnPlayerPerspectiveCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		if (Args.Num() == 0)
		{
			// Reset to local PC.
			GDebugObserverOverride = -1;
			UE_LOG(LogTemp, Log, TEXT("Sein.FogOfWar.Show.Player = LOCAL (reset)"));
			MarkAllFogDebugProxiesDirty();
			return;
		}

		const int32 Parsed = FCString::Atoi(*Args[0]);
		if (Parsed < 0 || Parsed > 255)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Sein.FogOfWar.Show.Player: expected 0-255, got %s"),
				*Args[0]);
			return;
		}

		GDebugObserverOverride = Parsed;
		UE_LOG(LogTemp, Log,
			TEXT("Sein.FogOfWar.Show.Player = Player(%d). "
				 "Use no-args to reset to local PC."),
			Parsed);
		MarkAllFogDebugProxiesDirty();
	}

	static void OnShowFogOfWarCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsFogOfWarShowFlagOn();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetFogOfWarShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.FogOfWar.Show = %s (ShowFlags.FogOfWar)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}
}

#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSFogOfWarModule::StartupModule()
{
	// Editor-side registrations (volume details + vision-stamp draw callback)
	// live in the SeinARTSFogOfWarEditor module — see file header.

#if UE_ENABLE_DEBUG_DRAWING
	if (!GShowFogOfWarCmd)
	{
		GShowFogOfWarCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.FogOfWar.Show"),
			TEXT("Toggle ShowFlags.FogOfWar across all viewports (same UX as Sein.Nav.Show). Drives the fog-of-war debug viz: non-PIE = whole grid red, PIE = local player controller's visible + explored cells. Usage: Sein.FogOfWar.Show [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowFogOfWarCommand),
			ECVF_Default);
	}
	if (!GPlayerPerspectiveCmd)
	{
		GPlayerPerspectiveCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.FogOfWar.Show.Player"),
			TEXT("Override the fog-of-war debug viewer to render a specific player's vision instead of the local player controller's. The sim is lockstep-symmetric — every client has every player's VisionGroup, so this is a local viewer toggle over shared data. Usage: Sein.FogOfWar.Show.Player <id 0-255>. No args → reset to local PC. id=0 = neutral (shows only what neutral-owned sources see)."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnPlayerPerspectiveCommand),
			ECVF_Default);
	}
	if (!GLayerPerspectiveCmd)
	{
		GLayerPerspectiveCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.FogOfWar.Show.Layer"),
			TEXT("Override the fog-of-war debug viewer to paint cells through a specific EVNNNNNN layer bit. Usage: Sein.FogOfWar.Show.Layer <bit 0-7>. 0 = E (Explored, yellow), 1 = V (Normal, cyan — default), 2..7 = N0..N5 (colors from VisionLayers settings). Blockers still render red when the baked BlockerLayerMask occludes the selected bit. No args → reset to V."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnLayerPerspectiveCommand),
			ECVF_Default);
	}
#endif
}

void FSeinARTSFogOfWarModule::ShutdownModule()
{
	// Editor-side teardown (volume details + vision-stamp draw callback)
	// lives in SeinARTSFogOfWarEditor — see file header.

#if UE_ENABLE_DEBUG_DRAWING
	if (GShowFogOfWarCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowFogOfWarCmd);
		GShowFogOfWarCmd = nullptr;
	}
	if (GPlayerPerspectiveCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GPlayerPerspectiveCmd);
		GPlayerPerspectiveCmd = nullptr;
	}
	if (GLayerPerspectiveCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GLayerPerspectiveCmd);
		GLayerPerspectiveCmd = nullptr;
	}
#endif
}

namespace UE::SeinARTSFogOfWar
{
	bool TryGetDebugObserverOverride(FSeinPlayerID& OutObserver)
	{
#if UE_ENABLE_DEBUG_DRAWING
		if (GDebugObserverOverride >= 0 && GDebugObserverOverride <= 255)
		{
			OutObserver = FSeinPlayerID(static_cast<uint8>(GDebugObserverOverride));
			return true;
		}
#endif
		return false;
	}

	bool TryGetDebugLayerOverride(int32& OutBitIndex)
	{
#if UE_ENABLE_DEBUG_DRAWING
		if (GDebugLayerOverride >= 0 && GDebugLayerOverride <= 7)
		{
			OutBitIndex = GDebugLayerOverride;
			return true;
		}
#endif
		return false;
	}

	FSeinPlayerID ResolveLocalObserverPlayerID(UWorld* World)
	{
		if (!World) return FSeinPlayerID();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC || !PC->IsLocalPlayerController()) continue;
			FStructProperty* Prop = FindFProperty<FStructProperty>(PC->GetClass(), TEXT("SeinPlayerID"));
			if (!Prop || Prop->Struct != FSeinPlayerID::StaticStruct()) return FSeinPlayerID();
			if (const FSeinPlayerID* Value = Prop->ContainerPtrToValuePtr<FSeinPlayerID>(PC))
			{
				return *Value;
			}
			return FSeinPlayerID();
		}
		return FSeinPlayerID();
	}

	FColor GetDebugLayerColor(int32 BitIndex)
	{
#if UE_ENABLE_DEBUG_DRAWING
		// E (0) + V (1) are framework-reserved bits — colors hardcoded here
		// and intentionally NOT driven by plugin settings (the "Normal" layer
		// has no slot, per DESIGN §12).
		if (BitIndex == 0) return FColor(255, 230, 0);  // Yellow — Explored
		if (BitIndex == 1) return FColor(0, 200, 255);  // Cyan — Normal (V)

		// Custom N0..N5 colors live in plugin settings so designers can
		// retune per-project without a code change.
		if (BitIndex >= 2 && BitIndex <= 7)
		{
			const int32 SlotIdx = BitIndex - 2;
			if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
			{
				if (Settings->VisionLayers.IsValidIndex(SlotIdx))
				{
					return Settings->VisionLayers[SlotIdx].DebugColor.ToFColor(/*bSRGB*/ true);
				}
			}
		}
#endif
		return FColor::White;
	}
}
