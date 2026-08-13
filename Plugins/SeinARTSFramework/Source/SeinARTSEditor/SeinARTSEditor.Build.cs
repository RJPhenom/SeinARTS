using UnrealBuildTool;

public class SeinARTSEditor : ModuleRules
{
    public SeinARTSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // Validators intentionally reuse descriptive file-local metadata
        // identifiers; keep their anonymous namespaces out of unity merges.
        bUseUnity = false;

        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // These modules own base classes and value types exposed by the
        // editor module's public factories, validators, details panels,
        // visualizers, thumbnails, and widget-asset definitions.
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "SlateCore",
            "UnrealEd",
            "AssetTools",
            "BlueprintGraph",
            "DataValidation",
            "PropertyEditor",
            "UMGEditor",
            "GameplayTags"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "InputCore",
            "AssetRegistry",       // SeinAutoTagGenerator collision scan + rename hooks
            "ClassViewer",
            "Kismet",
            "KismetCompiler",
            "GraphEditor",
            "EditorStyle",
            "Projects",
            "StructUtilsEditor",
            "StructViewer",
            "RenderCore",
            "ImageCore",
            "UMG",
            "AssetDefinition",
            "GameplayTagsEditor",  // SeinAutoTagGenerator persists auto-tags to INI via IGameplayTagsEditorModule
            "SeinARTSCore",
            "SeinARTSCoreEntity",
			"SeinARTSGraphNodes",     // USeinWidgetBlueprint lives in the UncookedOnly asset module
            "SeinARTSUIToolkit"
            // Optional system editor modules (SeinARTSFogOfWar's #if
            // WITH_EDITOR block, SeinARTSCoverEditor, future systems)
            // register their per-component-type draw delegates via
            // FSeinARTSEditorModule::RegisterComponentDataDraw at their
            // own StartupModule. SeinARTSEditor is intentionally ignorant
            // of which optional systems are loaded — no hard deps, no
            // includes, no build coupling. Disabling cover / FoW / nav
            // takes that module's draw layer with it cleanly.
            //
            // The unified bake entry point is a CallInEditor button on
            // ASeinLevelVolume ("Bake Level Data") — no details-panel
            // customization needed; layer modules contribute via the
            // substrate's provider registry, same decoupling.
        });
    }
}
