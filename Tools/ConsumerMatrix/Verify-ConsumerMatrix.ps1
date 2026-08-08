#Requires -Version 5.1
<#
.SYNOPSIS
  Proves SeinARTS from a clean, generated downstream C++ project.

.DESCRIPTION
  Creates isolated projects under Saved/ConsumerMatrix, copies only the
  selected production plugins (never their Binaries/Intermediate state), and
  builds the Editor and Shipping game targets, plus Client and Server when the
  engine distribution supports them. It then creates a consumer-owned map,
  generates the consumer-owned simulation-content manifest, loads the exact
  maps, cooks/packages them, smoke-loads the packaged game, and drives a real
  packaged listen-server/client/replay qualification for the Framework profile.

  No generated artifact is written to the repository's tracked Output or Docs
  trees. The generated projects are disposable evidence, not source fixtures.
#>
[CmdletBinding()]
param(
	[ValidateSet('Framework', 'Cover', 'Squad', 'MovementPlus', 'Full', 'All')]
	[string] $Profile = 'All',

	[string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',

	[switch] $SkipCook,

	[switch] $ReuseGenerated,

	# Diagnostic escape hatch. Release evidence must exercise the packaged
	# listen-server/client/resync/reconnect/replay flow.
	[switch] $SkipRuntimeQualification,

	# Useful only for local diagnosis on launcher-engine installations that do
	# not contain Epic's Client/Server target support. Release CI must omit it.
	[switch] $SkipClientServer
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$RunUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$GeneratedRoot = Join-Path $RepoRoot 'Saved\ConsumerMatrix'

foreach ($Required in @($BuildBat, $EditorCmd, $RunUat)) {
	if (-not (Test-Path -LiteralPath $Required)) {
		throw "Required UE 5.8 tool is missing: '$Required'."
	}
}

$Profiles = if ($Profile -eq 'All') {
	@('Framework', 'Cover', 'Squad', 'MovementPlus', 'Full')
} else {
	@($Profile)
}

function Write-Utf8NoBom([string] $Path, [string] $Text)
{
	$Parent = Split-Path -Parent $Path
	if ($Parent) {
		New-Item -ItemType Directory -Path $Parent -Force | Out-Null
	}
	[System.IO.File]::WriteAllText(
		$Path,
		$Text,
		[System.Text.UTF8Encoding]::new($false))
}

function Invoke-Checked(
	[string] $Description,
	[string] $Executable,
	[string[]] $Arguments)
{
	Write-Host "[ConsumerMatrix] $Description" -ForegroundColor Cyan
	& $Executable @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Description failed with exit code $LASTEXITCODE."
	}
}

function Invoke-ManifestBootstrap(
	[string] $ProfileName,
	[string] $Uproject,
	[string] $ScriptPath,
	[string] $ManifestPath,
	[string] $ProjectRoot)
{
	Write-Host `
		"[ConsumerMatrix] $ProfileName bootstrap manifest generation" `
		-ForegroundColor Cyan
	$BootstrapOutput = @(& $EditorCmd @(
		$Uproject,
		'-run=pythonscript',
		"-script=$ScriptPath",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout') 2>&1)
	$BootstrapExitCode = $LASTEXITCODE
	$BootstrapLines = @($BootstrapOutput | ForEach-Object { "$_" })
	$BootstrapLines | ForEach-Object { Write-Host $_ }
	$BootstrapLog = Join-Path `
		$ProjectRoot 'Saved\BootstrapManifest.log'
	Write-Utf8NoBom $BootstrapLog ($BootstrapLines -join "`r`n")

	if (-not (Test-Path -LiteralPath $ManifestPath)) {
		throw "$ProfileName bootstrap manifest generation produced no '$ManifestPath'."
	}
	if (-not ($BootstrapLines -match 'Generated simulation-content manifest')) {
		throw "$ProfileName bootstrap produced an asset but no generation-success record. See '$BootstrapLog'."
	}
	if ($BootstrapExitCode -eq 0) {
		return
	}

	# On the first-ever launch there is necessarily no configured manifest yet.
	# USeinWorldSubsystem reports that absence while the Python commandlet's
	# transient editor world starts, which makes UE return 1 even though the
	# builder subsequently succeeds. Permit only those two exact protocol errors;
	# the next editor invocation must start clean from the generated asset.
	$UnexpectedErrors = @($BootstrapLines | Where-Object {
		$_ -match 'Error:' -and
		$_ -notmatch 'Configured Simulation Content Manifest .* could not be loaded' -and
		$_ -notmatch 'pool-object codec manifest could not freeze'
	})
	if ($BootstrapExitCode -ne 1 -or $UnexpectedErrors.Count -gt 0) {
		throw "$ProfileName bootstrap manifest process failed unexpectedly (exit $BootstrapExitCode). See '$BootstrapLog'."
	}
	Write-Host `
		"[ConsumerMatrix] $ProfileName accepted the one-time empty-consumer bootstrap diagnostics." `
		-ForegroundColor DarkYellow
}

function Copy-CleanPlugin([string] $PluginName, [string] $ProjectRoot)
{
	$Source = Join-Path $RepoRoot "Plugins\$PluginName"
	$Destination = Join-Path $ProjectRoot "Plugins\$PluginName"
	if (-not (Test-Path -LiteralPath $Source)) {
		throw "Production plugin '$PluginName' is missing."
	}
	New-Item -ItemType Directory -Path $Destination -Force | Out-Null

	$Descriptor = Join-Path $Source "$PluginName.uplugin"
	Copy-Item -LiteralPath $Descriptor -Destination $Destination
	foreach ($DirectoryName in @('Source', 'Content', 'Config', 'Resources', 'Shaders')) {
		$Directory = Join-Path $Source $DirectoryName
		if (Test-Path -LiteralPath $Directory) {
			Copy-Item -LiteralPath $Directory -Destination $Destination -Recurse
		}
	}
}

function Refresh-ConsumerPlugins(
	[string[]] $PluginNames,
	[string] $ProjectRoot)
{
	$ResolvedGeneratedRoot = [System.IO.Path]::GetFullPath($GeneratedRoot)
	$ResolvedProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
	if (-not $ResolvedProjectRoot.StartsWith(
		$ResolvedGeneratedRoot + [System.IO.Path]::DirectorySeparatorChar,
		[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to refresh consumer plugins outside '$GeneratedRoot'."
	}
	foreach ($PluginName in $PluginNames) {
		$SourceRoot = Join-Path $RepoRoot "Plugins\$PluginName"
		$DestinationRoot = Join-Path $ProjectRoot "Plugins\$PluginName"
		New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null

		$SourceDescriptor = Join-Path $SourceRoot "$PluginName.uplugin"
		$DestinationDescriptor = Join-Path $DestinationRoot "$PluginName.uplugin"
		$SourceDescriptorInfo = Get-Item -LiteralPath $SourceDescriptor
		$DestinationDescriptorInfo = Get-Item `
			-LiteralPath $DestinationDescriptor -ErrorAction SilentlyContinue
		if (
			-not $DestinationDescriptorInfo -or
			$SourceDescriptorInfo.Length -ne $DestinationDescriptorInfo.Length -or
			$SourceDescriptorInfo.LastWriteTimeUtc -ne
				$DestinationDescriptorInfo.LastWriteTimeUtc
		) {
			Copy-Item -LiteralPath $SourceDescriptor `
				-Destination $DestinationDescriptor -Force
		}

		foreach ($DirectoryName in @(
			'Source', 'Content', 'Config', 'Resources', 'Shaders')) {
			$SourceDirectory = Join-Path $SourceRoot $DirectoryName
			$DestinationDirectory = Join-Path $DestinationRoot $DirectoryName
			if (-not (Test-Path -LiteralPath $SourceDirectory)) {
				if (Test-Path -LiteralPath $DestinationDirectory) {
					Remove-Item -LiteralPath $DestinationDirectory -Recurse -Force
				}
				continue
			}

			New-Item -ItemType Directory `
				-Path $DestinationDirectory -Force | Out-Null
			$SourceFiles = @{}
			foreach ($SourceFile in Get-ChildItem `
				-LiteralPath $SourceDirectory -Recurse -File) {
				$Relative = [System.IO.Path]::GetRelativePath(
					$SourceDirectory, $SourceFile.FullName)
				$SourceFiles[$Relative] = $SourceFile
			}
			foreach ($DestinationFile in Get-ChildItem `
				-LiteralPath $DestinationDirectory -Recurse -File) {
				$Relative = [System.IO.Path]::GetRelativePath(
					$DestinationDirectory, $DestinationFile.FullName)
				if (-not $SourceFiles.ContainsKey($Relative)) {
					Remove-Item -LiteralPath $DestinationFile.FullName -Force
				}
			}
			foreach ($Pair in $SourceFiles.GetEnumerator()) {
				$DestinationFile = Join-Path $DestinationDirectory $Pair.Key
				$DestinationInfo = Get-Item `
					-LiteralPath $DestinationFile -ErrorAction SilentlyContinue
				if (
					$DestinationInfo -and
					$DestinationInfo.Length -eq $Pair.Value.Length -and
					$DestinationInfo.LastWriteTimeUtc -eq
						$Pair.Value.LastWriteTimeUtc
				) {
					continue
				}
				New-Item -ItemType Directory `
					-Path (Split-Path -Parent $DestinationFile) `
					-Force | Out-Null
				Copy-Item -LiteralPath $Pair.Value.FullName `
					-Destination $DestinationFile -Force
			}
		}
	}
}

function Assert-NoHostGameDependency([string] $ProjectRoot)
{
	if (-not (Get-Command rg -ErrorAction SilentlyContinue)) {
		throw 'Consumer dependency scanning requires ripgrep (rg).'
	}
	$Forbidden = @(& rg -a -l -F '/Game/SeinARTS' $ProjectRoot `
		-g '!**/Binaries/**' -g '!**/Intermediate/**' -g '!**/Saved/**')
	$SearchExit = $LASTEXITCODE
	if ($SearchExit -gt 1) {
		throw "Consumer dependency scan failed with ripgrep exit code $SearchExit."
	}
	if ($Forbidden) {
		throw "Generated consumer contains forbidden host /Game/SeinARTS references:`n$($Forbidden -join "`n")"
	}
}

function Install-ConsumerRuntimeQualificationTemplates([string] $ProjectRoot)
{
	foreach ($TemplateName in @(
		'SeinConsumerQualificationSubsystem.h',
		'SeinConsumerQualificationSubsystem.cpp')) {
		$TemplatePath = Join-Path $PSScriptRoot "Templates\$TemplateName"
		if (-not (Test-Path -LiteralPath $TemplatePath)) {
			throw "Consumer runtime qualification template is missing: '$TemplatePath'."
		}
		Copy-Item -LiteralPath $TemplatePath `
			-Destination (Join-Path $ProjectRoot "Source\SeinConsumer\$TemplateName") `
			-Force
	}
}

function New-ConsumerProject([string] $ProfileName)
{
	$ProjectRoot = Join-Path $GeneratedRoot $ProfileName
	$ResolvedGeneratedRoot = [System.IO.Path]::GetFullPath($GeneratedRoot)
	$ResolvedProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
	if (-not $ResolvedProjectRoot.StartsWith(
			$ResolvedGeneratedRoot + [System.IO.Path]::DirectorySeparatorChar,
			[System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to clean consumer path outside '$ResolvedGeneratedRoot'."
	}
	if (Test-Path -LiteralPath $ProjectRoot) {
		Remove-Item -LiteralPath $ProjectRoot -Recurse -Force
	}
	New-Item -ItemType Directory -Path $ProjectRoot -Force | Out-Null

	$Plugins = @('SeinARTSFramework')
	$ModuleDependencies = @(
		'Core',
		'CoreUObject',
		'Engine',
		'SeinARTSFramework',
		'SeinARTSCoreEntity',
		'SeinARTSNet')
	$Definitions = @()
	$ExtraIncludes = @()
	$HeaderProof = @()
	if ($ProfileName -in @('MovementPlus', 'Full')) {
		$Plugins += 'SeinARTSMovementPlusExtension'
		$ModuleDependencies += 'SeinARTSMovementPlus'
		$Definitions += 'SEIN_CONSUMER_WITH_MOVEMENT_PLUS=1'
		$ExtraIncludes += '#include "Movement/SeinWheeledVehicleMovement.h"'
		$HeaderProof += '(void)USeinWheeledVehicleMovement::StaticClass();'
	}
	if ($ProfileName -in @('Cover', 'Full')) {
		$Plugins += 'SeinARTSCoverExtension'
		$ModuleDependencies += 'SeinARTSCover'
		$Definitions += 'SEIN_CONSUMER_WITH_COVER=1'
		$ExtraIncludes += '#include "Components/SeinCoverComponent.h"'
		$HeaderProof += '(void)FSeinCoverComponent::StaticStruct();'
	}
	if ($ProfileName -in @('Squad', 'Full')) {
		$Plugins += 'SeinARTSSquadExtension'
		$ModuleDependencies += 'SeinARTSSquad'
		$Definitions += 'SEIN_CONSUMER_WITH_SQUAD=1'
		$ExtraIncludes += '#include "SeinSquadDispatchResolver.h"'
		$HeaderProof += '(void)USeinSquadDispatchResolver::StaticClass();'
	}
	if ($ProfileName -eq 'Full') {
		$Plugins += 'SeinARTSCoverSquadExtension'
		$ModuleDependencies += 'SeinARTSCoverSquad'
		$Definitions += 'SEIN_CONSUMER_WITH_FULL_EXTENSIONS=1'
		$ExtraIncludes += '#include "SeinCoverAwareSquadDispatchResolver.h"'
		$HeaderProof += '(void)USeinCoverAwareSquadDispatchResolver::StaticClass();'
	}

	foreach ($PluginName in $Plugins) {
		Copy-CleanPlugin $PluginName $ProjectRoot
	}

	$PluginEntries = @(
		foreach ($PluginName in $Plugins) {
			[ordered]@{ Name = $PluginName; Enabled = $true }
		}
		[ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
		[ordered]@{ Name = 'EditorScriptingUtilities'; Enabled = $true }
		[ordered]@{ Name = 'OnlineSubsystemNull'; Enabled = $true }
	)
	$Uproject = [ordered]@{
		FileVersion = 3
		EngineAssociation = '5.8'
		Category = 'SeinARTS Consumer Matrix'
		Description = "Generated clean $ProfileName consumer"
		Modules = @([ordered]@{
			Name = 'SeinConsumer'
			Type = 'Runtime'
			LoadingPhase = 'Default'
		})
		Plugins = $PluginEntries
	}
	$UprojectPath = Join-Path $ProjectRoot 'SeinConsumer.uproject'
	Write-Utf8NoBom $UprojectPath ($Uproject | ConvertTo-Json -Depth 8)

	$TargetCommon = @'
using UnrealBuildTool;
using System.Collections.Generic;

public class SeinConsumerTarget : TargetRules
{
	public SeinConsumerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SeinConsumer");
	}
}
'@
	$TargetEditor = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerEditorTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerEditorTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Editor')
	$TargetClient = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerClientTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerClientTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Client')
	$TargetServer = $TargetCommon.Replace(
		'class SeinConsumerTarget', 'class SeinConsumerServerTarget').Replace(
		'SeinConsumerTarget(TargetInfo', 'SeinConsumerServerTarget(TargetInfo').Replace(
		'TargetType.Game', 'TargetType.Server')
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer.Target.cs') $TargetCommon
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerEditor.Target.cs') $TargetEditor
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerClient.Target.cs') $TargetClient
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumerServer.Target.cs') $TargetServer

	$QuotedDependencies = ($ModuleDependencies | Sort-Object -Unique |
		ForEach-Object { "`"$_`"" }) -join ', '
	$DefinitionLines = ($Definitions | ForEach-Object {
		"`t`tPublicDefinitions.Add(`"$_`");"
	}) -join "`r`n"
	$BuildCs = @"
using UnrealBuildTool;

public class SeinConsumer : ModuleRules
{
	public SeinConsumer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { $QuotedDependencies });
$DefinitionLines
	}
}
"@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer\SeinConsumer.Build.cs') $BuildCs

	$IncludeLines = $ExtraIncludes -join "`r`n"
	$ProofLines = ($HeaderProof | ForEach-Object { "`t$_" }) -join "`r`n"
	$ModuleCpp = @"
#include "Modules/ModuleManager.h"
#include "Abilities/SeinAbility.h"
$IncludeLines

namespace
{
	void CompileConsumerSurfaceProof()
	{
		(void)USeinAbility::StaticClass();
$ProofLines
	}
}

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SeinConsumer, "SeinConsumer");
"@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Source\SeinConsumer\SeinConsumer.cpp') $ModuleCpp
	$DefaultEngine = @'
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Game/Maps/ConsumerLobbyMap
EditorStartupMap=/Game/Maps/ConsumerLobbyMap
GlobalDefaultGameMode=/Script/SeinARTSFramework.SeinGameMode

[/Script/Engine.Engine]
WorldSettingsClassName=/Script/SeinARTSFramework.SeinWorldSettings

[/Script/Engine.RendererSettings]
r.AllowStaticLighting=False
r.GenerateMeshDistanceFields=False
r.DynamicGlobalIlluminationMethod=0
r.ReflectionMethod=0
r.RayTracing=False

[CoreRedirects]
+ClassRedirects=(OldName="/Script/SeinARTSEditor.SeinWidgetBlueprint",NewName="/Script/SeinARTSGraphNodes.SeinWidgetBlueprint")

[OnlineSubsystem]
DefaultPlatformService=Null

[OnlineSubsystemNull]
bAutoLoginAtStartup=True
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Config\DefaultEngine.ini') $DefaultEngine

	$DefaultGame = @'
[/Script/EngineSettings.GeneralProjectSettings]
ProjectID=E42D638747C4108CCF59B1A7AB1A57D4

[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
SimulationContentManifest=/Game/Generated/SeinSimulationContentManifest.SeinSimulationContentManifest
DefaultBrokerResolverClass=/Script/SeinARTSCoreEntity.SeinDefaultCommandBrokerResolver
NavigationClass=/Script/SeinARTSNavigation.SeinNavigationAStar
LevelDataClass=/Script/SeinARTSLevelData.SeinLevelDataDefault
FogOfWarClass=/Script/SeinARTSFogOfWar.SeinFogOfWarDefault
AvoidanceClass=/Script/SeinARTSMovement.SeinAvoidanceDefault
CollisionResolverClass=/Script/SeinARTSCoreEntity.SeinCollisionResolverDefault
DefaultFormation=/Script/SeinARTSCoreEntity.SeinRingFormation
FormationPreviewActorClass=/Script/SeinARTSFramework.SeinFormationPreviewActor
bNetworkingEnabled=True
TurnRate=10
InputDelayTurns=2
MaxPlayers=2
RelayActorClass=/Script/SeinARTSNet.SeinNetRelay
bDeterminismChecksEnabled=True
bConfigParityCheckEnabled=True
DeterminismCheckIntervalTurns=5
ReplayCheckpointIntervalTurns=5
ReplayTurnBatchSize=4
ReplayMaxFileSizeMiB=64
DroppedToAITakeoverSeconds=30.0
DebugFixedSessionSeed=12345
LobbyReconnectGraceSeconds=60.0

[/Script/UnrealEd.ProjectPackagingSettings]
+MapsToCook=(FilePath="/Game/Maps/ConsumerLobbyMap")
+MapsToCook=(FilePath="/Game/Maps/ConsumerMap")
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Config\DefaultGame.ini') $DefaultGame

	$CreateMapPy = @'
import unreal

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

lobby_path = "/Game/Maps/ConsumerLobbyMap"
if not unreal.EditorAssetLibrary.does_asset_exist(lobby_path):
    if not level_editor.new_level(lobby_path):
        raise RuntimeError("Could not create consumer-owned lobby map " + lobby_path)
if not unreal.EditorAssetLibrary.save_asset(lobby_path, only_if_is_dirty=False):
    raise RuntimeError("Could not save consumer-owned lobby map " + lobby_path)

match_path = "/Game/Maps/ConsumerMap"
if not unreal.EditorAssetLibrary.does_asset_exist(match_path):
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.new_level(match_path):
        raise RuntimeError("Could not create consumer-owned match map " + match_path)
else:
    if not level_editor.load_level(match_path):
        raise RuntimeError("Could not load consumer-owned match map " + match_path)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("auto_start_sim", False)
starts = [
    actor for actor in actor_editor.get_all_level_actors()
    if isinstance(actor, unreal.SeinPlayerStart)
]
if not starts:
    for slot, x in ((1, -200.0), (2, 200.0)):
        start = actor_editor.spawn_actor_from_class(
            unreal.SeinPlayerStart,
            unreal.Vector(x, 0.0, 100.0),
            unreal.Rotator(0.0, 0.0, 0.0)
        )
        if start is None:
            raise RuntimeError("Could not spawn SeinPlayerStart for slot " + str(slot))
        start.set_editor_property("player_slot", slot)
        start.set_actor_label("ConsumerPlayerStart_" + str(slot))
if not unreal.EditorAssetLibrary.save_asset(match_path, only_if_is_dirty=False):
    raise RuntimeError("Could not save consumer-owned match map " + match_path)
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'CreateConsumerMap.py') $CreateMapPy

	$GenerateManifestPy = @'
import unreal

manifest_path = "/Game/Generated/SeinSimulationContentManifest"
unreal.SystemLibrary.execute_console_command(
    None, "Sein.SimulationContent.GenerateManifest"
)
if not unreal.EditorAssetLibrary.does_asset_exist(manifest_path):
    raise RuntimeError("Manifest generation produced no " + manifest_path)
unreal.log("Verified generated manifest " + manifest_path)
'@
	Write-Utf8NoBom `
		(Join-Path $ProjectRoot 'GenerateSimulationContentManifest.py') `
		$GenerateManifestPy

	$VerifyMapPy = @'
import unreal

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for asset_path in ("/Game/Maps/ConsumerLobbyMap", "/Game/Maps/ConsumerMap"):
    if not level_editor.load_level(asset_path):
        raise RuntimeError("Could not load consumer-owned map " + asset_path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or not world.get_path_name().startswith(asset_path + "."):
        actual = "<none>" if world is None else world.get_path_name()
        raise RuntimeError(
            "Consumer map load resolved the wrong editor world: " + actual
        )
    if asset_path.endswith("ConsumerMap"):
        slots = sorted(
            actor.get_editor_property("player_slot")
            for actor in actor_editor.get_all_level_actors()
            if isinstance(actor, unreal.SeinPlayerStart)
        )
        if slots != [1, 2]:
            raise RuntimeError("Consumer match map has wrong player slots: " + str(slots))
    unreal.log("Verified loaded consumer world " + world.get_path_name())
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'VerifyConsumerMap.py') $VerifyMapPy

	Assert-NoHostGameDependency $ProjectRoot
	return [pscustomobject]@{
		Name = $ProfileName
		Root = $ProjectRoot
		Uproject = $UprojectPath
		Plugins = $Plugins
	}
}

function Enable-ConsumerRuntimeMapConfiguration([string] $ProjectRoot)
{
	$ConfigPath = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
	$ConfigText = Get-Content -Raw -LiteralPath $ConfigPath
	$Marker = '; ConsumerMatrix runtime-map configuration'
	if ($ConfigText.Contains($Marker)) {
		return
	}
	$RuntimeConfig = @'

; ConsumerMatrix runtime-map configuration
[/Script/SeinARTSCoreEntity.SeinARTSCoreSettings]
+AvailableMaps=(Map="/Game/Maps/ConsumerMap.ConsumerMap",DisplayName=NSLOCTEXT("[SeinConsumer]", "ConsumerMap", "Consumer Match"),SlotCount=2,TeamCount=2)
DefaultGameplayMap=/Game/Maps/ConsumerMap.ConsumerMap

'@
	Write-Utf8NoBom $ConfigPath ($ConfigText.TrimEnd() + $RuntimeConfig)
}

function Invoke-ConsumerProfile([string] $ProfileName)
{
	$StartedAt = [DateTime]::UtcNow
	$ExistingRoot = Join-Path $GeneratedRoot $ProfileName
	$ExistingUproject = Join-Path $ExistingRoot 'SeinConsumer.uproject'
	if ($ReuseGenerated -and (Test-Path -LiteralPath $ExistingUproject)) {
		$ExistingPlugins = @('SeinARTSFramework')
		if ($ProfileName -in @('MovementPlus', 'Full')) {
			$ExistingPlugins += 'SeinARTSMovementPlusExtension'
		}
		if ($ProfileName -in @('Cover', 'Full')) {
			$ExistingPlugins += 'SeinARTSCoverExtension'
		}
		if ($ProfileName -in @('Squad', 'Full')) {
			$ExistingPlugins += 'SeinARTSSquadExtension'
		}
		if ($ProfileName -eq 'Full') {
			$ExistingPlugins += 'SeinARTSCoverSquadExtension'
		}
		$Project = [pscustomobject]@{
			Name = $ProfileName
			Root = $ExistingRoot
			Uproject = $ExistingUproject
			Plugins = $ExistingPlugins
		}
	} else {
		$Project = New-ConsumerProject $ProfileName
	}
	if ($ReuseGenerated) {
		# Reuse keeps expensive generated maps and build products, but source
		# evidence must always come from the current checkout.
		Refresh-ConsumerPlugins $Project.Plugins $Project.Root
	}
	Install-ConsumerRuntimeQualificationTemplates $Project.Root
	$CommonBuildArgs = @('Win64', 'Development', "-Project=$($Project.Uproject)", '-WaitMutex')
	Invoke-Checked "$ProfileName Editor build" $BuildBat (@('SeinConsumerEditor') + $CommonBuildArgs)
	if (-not $SkipClientServer) {
		Invoke-Checked "$ProfileName Client build" $BuildBat (@('SeinConsumerClient') + $CommonBuildArgs)
		Invoke-Checked "$ProfileName Server build" $BuildBat (@('SeinConsumerServer') + $CommonBuildArgs)
	}

	# Bootstrap the configured manifest before constructing a UWorld. The
	# framework intentionally validates manifest ownership at world startup;
	# a generated consumer must satisfy that contract before its first map is
	# created, then regenerate after the map exists so authored world content is
	# part of the final profile.
	$ManifestScript = Join-Path `
		$Project.Root 'GenerateSimulationContentManifest.py'
	$ManifestPath = Join-Path `
		$Project.Root 'Content\Generated\SeinSimulationContentManifest.uasset'
	Invoke-ManifestBootstrap `
		$ProfileName `
		$Project.Uproject `
		$ManifestScript `
		$ManifestPath `
		$Project.Root

	$MapPath = Join-Path $Project.Root 'Content\Maps\ConsumerMap.umap'
	$LobbyMapPath = Join-Path `
		$Project.Root 'Content\Maps\ConsumerLobbyMap.umap'
	if (-not ($ReuseGenerated -and
		(Test-Path -LiteralPath $MapPath) -and
		(Test-Path -LiteralPath $LobbyMapPath))) {
		Invoke-Checked "$ProfileName consumer map generation" $EditorCmd @(
			$Project.Uproject,
			'-run=pythonscript',
			"-script=$(Join-Path $Project.Root 'CreateConsumerMap.py')",
			'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')
	}
	if (-not (Test-Path -LiteralPath $MapPath) -or
		-not (Test-Path -LiteralPath $LobbyMapPath)) {
		throw "$ProfileName map generation did not produce both consumer maps."
	}
	Enable-ConsumerRuntimeMapConfiguration $Project.Root

	# Regenerate after the map exists. A builder failure is logged as Error and
	# makes the Python commandlet fail even though the bootstrap asset exists.
	Invoke-Checked "$ProfileName manifest generation" $EditorCmd @(
		$Project.Uproject,
		'-run=pythonscript',
		"-script=$ManifestScript",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')
	if (-not (Test-Path -LiteralPath $ManifestPath)) {
		throw "$ProfileName manifest generation produced no '$ManifestPath'."
	}

	Assert-NoHostGameDependency $Project.Root
	Invoke-Checked "$ProfileName uncooked map load" $EditorCmd @(
		$Project.Uproject,
		'-run=pythonscript',
		"-script=$(Join-Path $Project.Root 'VerifyConsumerMap.py')",
		'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')

	# Build Shipping only after the generated maps and final map-selection
	# config exist, so this binary is the exact package/runtime candidate.
	Invoke-Checked "$ProfileName Shipping game build" $BuildBat @(
		'SeinConsumer', 'Win64', 'Shipping',
		"-Project=$($Project.Uproject)", '-WaitMutex')

	$ArchiveRoot = Join-Path $Project.Root 'Saved\Packaged'
	if (-not $SkipCook) {
		Invoke-Checked "$ProfileName clean cook/package" $RunUat @(
			'BuildCookRun',
			"-project=$($Project.Uproject)",
			'-noP4', '-unattended', '-utf8output',
			'-platform=Win64', '-clientconfig=Shipping',
			'-cook', '-stage', '-pak', '-archive',
			"-archivedirectory=$ArchiveRoot",
			'-map=/Game/Maps/ConsumerLobbyMap+/Game/Maps/ConsumerMap',
			'-skipbuild')

		$GameExe = Get-ChildItem -LiteralPath $ArchiveRoot -Recurse -File |
			Where-Object {
				$_.Name -eq 'SeinConsumer-Win64-Shipping.exe'
			} |
			Select-Object -First 1
		if (-not $GameExe) {
			throw "$ProfileName package contains no Shipping game binary."
		}
		$SmokeOut = Join-Path $Project.Root 'Saved\PackagedSmoke.stdout.log'
		$SmokeErr = Join-Path $Project.Root 'Saved\PackagedSmoke.stderr.log'
		$Process = Start-Process -FilePath $GameExe.FullName `
			-ArgumentList @('-unattended', '-nullrhi', '-nosound', '-nosplash') `
			-RedirectStandardOutput $SmokeOut -RedirectStandardError $SmokeErr `
			-WindowStyle Hidden -PassThru
		if (-not $Process.WaitForExit(10000)) {
			# Shipping strips the interactive quit console command. Launch the real
			# game binary (not the root bootstrap executable) so this Process object
			# owns the process that maps the IoStore files. Packaged Game targets boot
			# their configured default map rather than honoring a raw map argument;
			# surviving this window proves that the executable mounted its IoStore
			# payload and opened the cooked lobby without an immediate module/content
			# crash. The Framework runtime leg explicitly travels to the match map.
			Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
			$Process.WaitForExit()
			$Process.Refresh()
			Write-Host `
				"[ConsumerMatrix] $ProfileName packaged game survived the 10-second map startup window." `
				-ForegroundColor Green
		}
		else {
			$Process.Refresh()
			if ($Process.ExitCode -ne 0) {
				throw "$ProfileName packaged map smoke load failed with exit code $($Process.ExitCode)."
			}
		}

		if ($ProfileName -eq 'Framework' -and
			-not $SkipRuntimeQualification) {
			$RuntimeScript = Join-Path `
				$PSScriptRoot 'Invoke-PackagedRuntimeQualification.ps1'
			if (-not (Test-Path -LiteralPath $RuntimeScript)) {
				throw "Packaged runtime qualification script is missing: '$RuntimeScript'."
			}
			& $RuntimeScript `
				-GameExecutable $GameExe.FullName `
				-ProjectRoot $Project.Root
			if ($LASTEXITCODE -ne 0) {
				throw "$ProfileName packaged runtime qualification failed with exit code $LASTEXITCODE."
			}
			$RuntimeResult = Join-Path `
				$Project.Root 'Saved\RuntimeQualification\runtime-result.json'
			if (-not (Test-Path -LiteralPath $RuntimeResult)) {
				throw "$ProfileName runtime qualification produced no formal result."
			}
		}
	}

	$Result = [ordered]@{
		schemaVersion = 1
		profile = $ProfileName
		plugins = $Project.Plugins
		engine = $EngineRoot
		editorBuild = 'Passed'
		clientBuild = if ($SkipClientServer) { 'Skipped' } else { 'Passed' }
		serverBuild = if ($SkipClientServer) { 'Skipped' } else { 'Passed' }
		shippingGameBuild = 'Passed'
		consumerMaps = @(
			'/Game/Maps/ConsumerLobbyMap',
			'/Game/Maps/ConsumerMap')
		consumerManifest = '/Game/Generated/SeinSimulationContentManifest'
		uncookedLoad = 'Passed'
		cookAndPackagedLoad = if ($SkipCook) { 'Skipped' } else { 'Passed' }
		packagedRuntimeQualification = if ($ProfileName -ne 'Framework') {
			'NotApplicable'
		} elseif ($SkipCook -or $SkipRuntimeQualification) {
			'Skipped'
		} else {
			'Passed'
		}
		startedAtUtc = $StartedAt.ToString('o')
		completedAtUtc = [DateTime]::UtcNow.ToString('o')
	}
	$ResultPath = Join-Path $Project.Root 'matrix-result.json'
	Write-Utf8NoBom $ResultPath ($Result | ConvertTo-Json -Depth 6)
	Write-Host "[ConsumerMatrix] $ProfileName passed: $ResultPath" -ForegroundColor Green
}

New-Item -ItemType Directory -Path $GeneratedRoot -Force | Out-Null
foreach ($ProfileName in $Profiles) {
	Invoke-ConsumerProfile $ProfileName
}
