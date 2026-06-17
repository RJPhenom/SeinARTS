using UnrealBuildTool;

public class SeinARTSFogOfWar : ModuleRules
{
    public SeinARTSFogOfWar(ReadOnlyTargetRules Target) : base(Target)
    {
        // PUBLIC because SeinFogOfWarDefault.h (a public header) inherits
        // ISeinLevelLayerProvider — the include path must propagate to any
        // module that includes fog headers (same rationale as nav's dep).
        PublicDependencyModuleNames.AddRange(new string[] {
            "SeinARTSLevelData"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags",
            // Terrain-scaled vision: TickStamps samples the baked per-cell terrain type
            // under each vision source (USeinNavigation::GetTerrainTypeAt) and scales the
            // stamp radius by the type's VisionMultiplier. Nav owns the runtime terrain grid.
            "SeinARTSNavigation",
            "RenderCore", "RHI"
        });

        // Editor-side integrations (volume details panel + entity-bridge
        // vision-stamp draw callback) live in the dedicated
        // SeinARTSFogOfWarEditor module now. The deps below are still needed
        // by this Runtime module because the show-flag setter inside the
        // `Sein.FogOfWar.Show` console command (in the UE_ENABLE_DEBUG_DRAWING
        // block) reaches GEditor + FEditorViewportClient::Invalidate to push
        // ShowFlags.FogOfWar onto the level-editor viewports.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd",          // GEditor
                "LevelEditor"        // FLevelEditorViewportClient
            });
        }
    }
}
