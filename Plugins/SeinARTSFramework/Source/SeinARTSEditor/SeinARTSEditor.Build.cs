using UnrealBuildTool;

public class SeinARTSEditor : ModuleRules
{
    public SeinARTSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // Validators intentionally reuse descriptive file-local metadata
        // identifiers; keep their anonymous namespaces out of unity merges.
        bUseUnity = false;

        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "InputCore",
            "UnrealEd",
            "AssetRegistry",       // SeinAutoTagGenerator collision scan + rename hooks
            "AssetTools",
            "ClassViewer",
            "Kismet",
            "KismetCompiler",
            "GraphEditor",
            "BlueprintGraph",
            "DataValidation",      // movement-mode determinism validator (UEditorValidatorBase)
            "EditorStyle",
            "Projects",
            "PropertyEditor",
            "StructUtilsEditor",
            "StructViewer",
            "RenderCore",
            "ImageCore",
            "UMG",
            "UMGEditor",
            "AssetDefinition",
            "GameplayTags",
            "GameplayTagsEditor",  // SeinAutoTagGenerator persists auto-tags to INI via IGameplayTagsEditorModule
            "SeinARTSCore",
            "SeinARTSCoreEntity",
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
