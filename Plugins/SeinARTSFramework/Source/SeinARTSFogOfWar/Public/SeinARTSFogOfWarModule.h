#pragma once

#include "Modules/ModuleManager.h"
#include "Core/SeinPlayerID.h"

class FSeinARTSFogOfWarModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

namespace UE::SeinARTSFogOfWar
{
	/** Debug proxy observer override, driven by the
	 *  `Sein.FogOfWar.Show.Player <id>` console command.
	 *  Writes OutObserver + returns true when an override is active (the
	 *  debug component should render that player's view instead of the
	 *  local PC's). Returns false in default state. Shipping: no-op false. */
	SEINARTSFOGOFWAR_API bool TryGetDebugObserverOverride(FSeinPlayerID& OutObserver);

	/** Debug proxy layer override, driven by the
	 *  `Sein.FogOfWar.Show.Layer <bit>` console command.
	 *  OutBitIndex is in [0, 7]: 0=E (Explored), 1=V (Normal), 2..7=N0..N5.
	 *  Returns false in default state → impl uses the V bit.
	 *  Shipping: no-op false. */
	SEINARTSFOGOFWAR_API bool TryGetDebugLayerOverride(int32& OutBitIndex);

	/** Resolve the debug paint color for a given EVNNNNNN bit index.
	 *   bit 0 (E)   → yellow
	 *   bit 1 (V)   → cyan
	 *   bit 2..7    → `USeinARTSCoreSettings::VisionLayers[bit-2].DebugColor`
	 *  Out-of-range bits return white. Shipping: returns white. */
	SEINARTSFOGOFWAR_API FColor GetDebugLayerColor(int32 BitIndex);

	/** Reflectively read the local PC's `SeinPlayerID` property. Central
	 *  lookup used by the debug proxy, the visibility subsystem, and the
	 *  reader BPFL — avoids a module dep on SeinARTSFramework (which owns
	 *  the concrete PC type). Returns neutral (id 0) when no local PC
	 *  exists or the PC's class doesn't expose `SeinPlayerID` as an
	 *  FStructProperty. */
	SEINARTSFOGOFWAR_API FSeinPlayerID ResolveLocalObserverPlayerID(UWorld* World);
}
