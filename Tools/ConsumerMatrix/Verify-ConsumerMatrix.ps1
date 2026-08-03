#Requires -Version 5.1
<#
.SYNOPSIS
  Proves SeinARTS from a clean, generated downstream C++ project.

.DESCRIPTION
  Creates isolated projects under Saved/ConsumerMatrix, copies only the
  selected production plugins (never their Binaries/Intermediate state), and
  builds the Editor, Client, Server, and Shipping game targets. It then creates
  a consumer-owned map, generates the consumer-owned simulation-content
  manifest, loads the map, cooks/packages it, and smoke-loads the packaged game.

  No generated artifact is written to the repository's tracked Output or Docs
  trees. The generated projects are disposable evidence, not source fixtures.
#>
[CmdletBinding()]
param(
	[ValidateSet('Framework', 'MovementPlus', 'Full', 'All')]
	[string] $Profile = 'All',

	[switch] $SkipCook,

	[switch] $ReuseGenerated,

	# Useful only for local diagnosis on launcher-engine installations that do
	# not contain Epic's Client/Server target support. Release CI must omit it.
	[switch] $SkipClientServer
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EngineRoot = 'C:\Program Files\Epic Games\UE_5.7'
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$RunUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$GeneratedRoot = Join-Path $RepoRoot 'Saved\ConsumerMatrix'

foreach ($Required in @($BuildBat, $EditorCmd, $RunUat)) {
	if (-not (Test-Path -LiteralPath $Required)) {
		throw "Required UE 5.7 tool is missing: '$Required'."
	}
}

$Profiles = if ($Profile -eq 'All') {
	@('Framework', 'MovementPlus', 'Full')
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
	$ModuleDependencies = @('Core', 'CoreUObject', 'Engine', 'SeinARTSFramework', 'SeinARTSCoreEntity')
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
	if ($ProfileName -eq 'Full') {
		$Plugins += @('SeinARTSSquadExtension', 'SeinARTSCoverExtension')
		$ModuleDependencies += @(
			'SeinARTSSquad',
			'SeinARTSCover',
			'SeinARTSCoverSquad')
		$Definitions += 'SEIN_CONSUMER_WITH_FULL_EXTENSIONS=1'
		$ExtraIncludes += @(
			'#include "SeinSquadDispatchResolver.h"',
			'#include "Components/SeinCoverComponent.h"',
			'#include "SeinCoverAwareSquadDispatchResolver.h"')
		$HeaderProof += @(
			'(void)USeinSquadDispatchResolver::StaticClass();',
			'(void)FSeinCoverComponent::StaticStruct();',
			'(void)USeinCoverAwareSquadDispatchResolver::StaticClass();')
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
	)
	$Uproject = [ordered]@{
		FileVersion = 3
		EngineAssociation = '5.7'
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
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
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
GameDefaultMap=/Game/Maps/ConsumerMap
EditorStartupMap=/Game/Maps/ConsumerMap
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

[/Script/UnrealEd.ProjectPackagingSettings]
+MapsToCook=(FilePath="/Game/Maps/ConsumerMap")
'@
	Write-Utf8NoBom (Join-Path $ProjectRoot 'Config\DefaultGame.ini') $DefaultGame

	$CreateMapPy = @'
import unreal

asset_path = "/Game/Maps/ConsumerMap"
if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.new_level(asset_path):
        raise RuntimeError("Could not create consumer-owned map " + asset_path)
if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
    raise RuntimeError("Could not save consumer-owned map " + asset_path)
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

	Assert-NoHostGameDependency $ProjectRoot
	return [pscustomobject]@{
		Name = $ProfileName
		Root = $ProjectRoot
		Uproject = $UprojectPath
		Plugins = $Plugins
	}
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
		if ($ProfileName -eq 'Full') {
			$ExistingPlugins += @(
				'SeinARTSSquadExtension',
				'SeinARTSCoverExtension')
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
	$CommonBuildArgs = @('Win64', 'Development', "-Project=$($Project.Uproject)", '-WaitMutex')
	Invoke-Checked "$ProfileName Editor build" $BuildBat (@('SeinConsumerEditor') + $CommonBuildArgs)
	if (-not $SkipClientServer) {
		Invoke-Checked "$ProfileName Client build" $BuildBat (@('SeinConsumerClient') + $CommonBuildArgs)
		Invoke-Checked "$ProfileName Server build" $BuildBat (@('SeinConsumerServer') + $CommonBuildArgs)
	}
	Invoke-Checked "$ProfileName Shipping game build" $BuildBat @(
		'SeinConsumer', 'Win64', 'Shipping',
		"-Project=$($Project.Uproject)", '-WaitMutex')

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
	if (-not ($ReuseGenerated -and (Test-Path -LiteralPath $MapPath))) {
		Invoke-Checked "$ProfileName consumer map generation" $EditorCmd @(
			$Project.Uproject,
			'-run=pythonscript',
			"-script=$(Join-Path $Project.Root 'CreateConsumerMap.py')",
			'-unattended', '-nop4', '-nosplash', '-nullrhi', '-stdout')
	}
	if (-not (Test-Path -LiteralPath $MapPath)) {
		throw "$ProfileName map generation produced no '$MapPath'."
	}

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
		'/Game/Maps/ConsumerMap',
		'-game', '-unattended', '-nop4', '-nosplash', '-nullrhi', '-nosound',
		'-ExecCmds=quit')

	$ArchiveRoot = Join-Path $Project.Root 'Saved\Packaged'
	if (-not $SkipCook) {
		Invoke-Checked "$ProfileName clean cook/package" $RunUat @(
			'BuildCookRun',
			"-project=$($Project.Uproject)",
			'-noP4', '-unattended', '-utf8output',
			'-platform=Win64', '-clientconfig=Shipping',
			'-cook', '-stage', '-pak', '-archive',
			"-archivedirectory=$ArchiveRoot",
			'-map=/Game/Maps/ConsumerMap',
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
			-ArgumentList @('/Game/Maps/ConsumerMap', '-unattended', '-nullrhi', '-nosound', '-nosplash') `
			-RedirectStandardOutput $SmokeOut -RedirectStandardError $SmokeErr `
			-WindowStyle Hidden -PassThru
		if (-not $Process.WaitForExit(10000)) {
			# Shipping strips the interactive quit console command. Launch the real
			# game binary (not the root bootstrap executable) so this Process object
			# owns the process that maps the IoStore files. Surviving a
			# bounded startup window proves that the executable mounted its IoStore
			# payload and opened the requested cooked map without an immediate
			# module/content crash; terminate the disposable smoke process ourselves.
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
		consumerMap = '/Game/Maps/ConsumerMap'
		consumerManifest = '/Game/Generated/SeinSimulationContentManifest'
		uncookedLoad = 'Passed'
		cookAndPackagedLoad = if ($SkipCook) { 'Skipped' } else { 'Passed' }
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
