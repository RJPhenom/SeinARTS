#pragma once

#include "Modules/ModuleManager.h"
#include "EngineDefines.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinSimulationContentRegistry.h"

class FSeinARTSNavigationModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid();
	}

private:
	FSeinCanonicalStateRegistrationHandle CanonicalStateRegistrationHandle;
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
};

#if UE_ENABLE_DEBUG_DRAWING
class UWorld;

namespace UE::SeinARTSNavigation
{
	/** Read the current `Sein.Nav.Show.Layer` console override. Returns true
	 *  + writes the selected bit (0..7) when active; false when no override
	 *  is set (reset state — show all blockers). USeinNavigationAStar reads
	 *  this from CollectDebugBlockerCells to filter the rendered orange
	 *  cells to those that affect the chosen agent layer. */
	SEINARTSNAVIGATION_API bool TryGetDebugNavLayerOverride(int32& OutBitIndex);

	/** True iff `World`'s viewport currently has the Navigation showflag on
	 *  (the same bit toggled by UE's 'P' hotkey, `showflag.navigation`, and
	 *  `Sein.Nav.Show`). Module-public so SeinARTSMovement's active-move
	 *  ticker can gate per-world drawing on the same toggle that drives the
	 *  cell-quad scene proxy — designers see one unified "show nav debug" UX
	 *  even though the rendering work is split across both modules.
	 *
	 *  Strict per-world gating: PIE / standalone consults the game viewport;
	 *  editor consults editor viewports rendering the matching world. No
	 *  cross-pollination — the designer toggles each independently. */
	SEINARTSNAVIGATION_API bool IsNavigationShowFlagOnForWorld(UWorld* World);
}
#endif
