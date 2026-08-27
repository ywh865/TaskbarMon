[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectDirectory = Join-Path $repositoryRoot 'TaskbarMon'
$projectPath = Join-Path $projectDirectory 'TaskbarMon.vcxproj'
$filtersPath = Join-Path $projectDirectory 'TaskbarMon.vcxproj.filters'
$solutionPath = Join-Path $repositoryRoot 'TaskbarMon.sln'

function Read-ProjectXml([string] $path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required project file is missing: $path"
    }

    try {
        return [xml](Get-Content -LiteralPath $path -Raw)
    }
    catch {
        throw "Invalid XML in $path`: $($_.Exception.Message)"
    }
}

function Get-ProjectItems($xml) {
    $namespace = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $namespace.AddNamespace('msbuild', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $itemNames = @('ClCompile', 'ClInclude', 'ResourceCompile', 'Resource', 'None', 'Image', 'Text')
    return @(
        $xml.SelectNodes('//msbuild:ItemGroup/*[@Include]', $namespace) |
            Where-Object { $itemNames -contains $_.Name } |
            ForEach-Object { '{0}|{1}' -f $_.Name, $_.Include }
    )
}

function Assert-NoDuplicates([string[]] $items, [string] $label) {
    $duplicates = @($items | Group-Object | Where-Object Count -gt 1)
    if ($duplicates.Count -gt 0) {
        $values = $duplicates | ForEach-Object Name
        throw "$label contains duplicate entries: $($values -join ', ')"
    }
}

$projectXml = Read-ProjectXml $projectPath
$filtersXml = Read-ProjectXml $filtersPath
$solutionText = Get-Content -LiteralPath $solutionPath -Raw
$projectItems = @(Get-ProjectItems $projectXml)
$filterItems = @(Get-ProjectItems $filtersXml)

if ($projectItems.Count -eq 0) {
    throw 'TaskbarMon.vcxproj does not declare any source or resource files.'
}

Assert-NoDuplicates $projectItems 'TaskbarMon.vcxproj'
Assert-NoDuplicates $filterItems 'TaskbarMon.vcxproj.filters'

$missingFromFilters = @(Compare-Object -ReferenceObject $projectItems -DifferenceObject $filterItems -PassThru |
    Where-Object SideIndicator -eq '<=')
$orphanedInFilters = @(Compare-Object -ReferenceObject $projectItems -DifferenceObject $filterItems -PassThru |
    Where-Object SideIndicator -eq '=>')
if ($missingFromFilters.Count -gt 0 -or $orphanedInFilters.Count -gt 0) {
    throw "Project/filter item mismatch. Missing filters: $($missingFromFilters -join ', '); orphaned filters: $($orphanedInFilters -join ', ')"
}

foreach ($item in $projectItems) {
    $separator = $item.IndexOf('|')
    $include = $item.Substring($separator + 1)
    $resolvedPath = Join-Path $projectDirectory $include
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        throw "Project item does not exist on disk: $include"
    }
}

$namespace = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
$namespace.AddNamespace('msbuild', 'http://schemas.microsoft.com/developer/msbuild/2003')
$forbiddenHardwareProject = 'OpenHardwareMonitorApi'
if ($solutionText -match [regex]::Escape($forbiddenHardwareProject)) {
    throw "$forbiddenHardwareProject must not be present in TaskbarMon.sln."
}

$forbiddenHardwareReferences = @(Select-String -LiteralPath $projectPath -Pattern 'LibreHardwareMonitorLib\.dll|OpenHardwareMonitorApi\.vcxproj' -AllMatches)
if ($forbiddenHardwareReferences.Count -gt 0) {
    throw 'TaskbarMon.vcxproj must not reference or publish the hardware-monitor DLL project.'
}

$projectReferences = @($projectXml.SelectNodes('//msbuild:ProjectReference[@Include]', $namespace))
$forbiddenProjectReferences = @($projectReferences | Where-Object {
    $_.Include -match [regex]::Escape($forbiddenHardwareProject)
})
if ($forbiddenProjectReferences.Count -gt 0) {
    $references = $forbiddenProjectReferences | ForEach-Object Include
    throw "TaskbarMon.vcxproj must not reference $forbiddenHardwareProject`: $($references -join ', ')"
}

$configurations = @($projectXml.SelectNodes('//msbuild:ProjectConfiguration[@Include]', $namespace) | ForEach-Object Include)
if ($configurations.Count -eq 0) {
    throw 'No solution configurations were found in TaskbarMon.vcxproj.'
}

$projectGuidNode = $projectXml.SelectSingleNode('//msbuild:ProjectGuid', $namespace)
if ($null -eq $projectGuidNode -or [string]::IsNullOrWhiteSpace($projectGuidNode.InnerText)) {
    throw 'TaskbarMon.vcxproj does not define a project GUID.'
}
$projectGuid = $projectGuidNode.InnerText.Trim('{}')
$solutionConfigurations = @()
$inSolutionConfigurationSection = $false
foreach ($line in ($solutionText -split '\r?\n')) {
    if ($line -match '^\s*GlobalSection\(SolutionConfigurationPlatforms\)\s*=') {
        $inSolutionConfigurationSection = $true
        continue
    }
    if ($inSolutionConfigurationSection -and $line -match '^\s*EndGlobalSection') {
        $inSolutionConfigurationSection = $false
        continue
    }
    if ($inSolutionConfigurationSection -and $line -match '^\s*(?<configuration>[^=]+?)\s*=') {
        $solutionConfigurations += $Matches.configuration.Trim()
    }
}
if ($solutionConfigurations.Count -eq 0) {
    throw 'TaskbarMon.sln does not define solution configurations.'
}

$solutionProjectMappings = @{}
$mappingPattern = '^\s*\{' + [regex]::Escape($projectGuid) + '\}\.(?<solution>.+)\.(?<kind>ActiveCfg|Build\.0)\s*=\s*(?<target>.+?)\s*$'
foreach ($line in ($solutionText -split '\r?\n')) {
    if ($line -match $mappingPattern) {
        $key = $Matches.solution.Trim()
        if (-not $solutionProjectMappings.ContainsKey($key)) {
            $solutionProjectMappings[$key] = @{}
        }
        $solutionProjectMappings[$key][$Matches.kind] = $Matches.target.Trim()
    }
}
foreach ($solutionConfiguration in $solutionConfigurations) {
    if (-not $solutionProjectMappings.ContainsKey($solutionConfiguration)) {
        throw "TaskbarMon.sln has no TaskbarMon mapping for $solutionConfiguration."
    }
    $mapping = $solutionProjectMappings[$solutionConfiguration]
    if (-not $mapping.ContainsKey('ActiveCfg') -or $configurations -notcontains $mapping.ActiveCfg) {
        throw "TaskbarMon.sln maps $solutionConfiguration to an unknown project configuration."
    }
    if (-not $mapping.ContainsKey('Build.0')) {
        throw "TaskbarMon.sln does not build TaskbarMon for $solutionConfiguration."
    }
}

$definitionGroups = @($projectXml.SelectNodes('//msbuild:ItemDefinitionGroup[@Condition]', $namespace) |
    Where-Object { $_.Condition -match '\$\(Configuration\)\|\$\(Platform\)' })
foreach ($configuration in $configurations) {
    $matchingGroups = @($definitionGroups | Where-Object { $_.Condition -match [regex]::Escape($configuration) })
    if ($matchingGroups.Count -ne 1) {
        throw "Expected one configuration item-definition group for $configuration, found $($matchingGroups.Count)."
    }

    $definitions = $matchingGroups[0].SelectSingleNode('msbuild:ClCompile/msbuild:PreprocessorDefinitions', $namespace)
    if ($null -eq $definitions -or $definitions.InnerText -notmatch '(^|;)WITHOUT_TEMPERATURE(;|$)') {
        throw "WITHOUT_TEMPERATURE is missing from the compiler definitions for $configuration."
    }

    $uacLevel = $matchingGroups[0].SelectSingleNode('msbuild:Link/msbuild:UACExecutionLevel', $namespace)
    if ($null -eq $uacLevel -or $uacLevel.InnerText -ne 'AsInvoker') {
        throw "UACExecutionLevel must be AsInvoker for $configuration."
    }
}

$securityGroup = $projectXml.SelectSingleNode('//msbuild:ItemDefinitionGroup[@Label="SecurityHardening"]', $namespace)
if ($null -eq $securityGroup) {
    throw 'The global SecurityHardening item-definition group is missing.'
}

$securitySettings = @{
    'ClCompile/SDLCheck' = 'true'
    'ClCompile/BufferSecurityCheck' = 'true'
    'ClCompile/ConformanceMode' = 'true'
    'Link/DataExecutionPrevention' = 'true'
    'Link/RandomizedBaseAddress' = 'true'
    'Link/ControlFlowGuard' = 'Guard'
}
foreach ($setting in $securitySettings.GetEnumerator()) {
    $node = $securityGroup.SelectSingleNode("msbuild:$($setting.Key.Replace('/', '/msbuild:'))", $namespace)
    if ($null -eq $node -or $node.InnerText -ne $setting.Value) {
        throw "SecurityHardening must set $($setting.Key) to $($setting.Value)."
    }
}

$warningPolicy = $projectXml.SelectSingleNode('//msbuild:ItemDefinitionGroup[@Label="ReleaseWarningPolicy"]', $namespace)
$warningsAsErrors = if ($null -eq $warningPolicy) { $null } else {
    $warningPolicy.SelectSingleNode('msbuild:ClCompile/msbuild:TreatWarningAsError', $namespace)
}
$warningCondition = if ($null -eq $warningPolicy) { '' } else { [string]$warningPolicy.Condition }
if ($null -eq $warningPolicy -or
    $warningCondition -notmatch "'\$\(Configuration\)'=='Release'" -or
    $warningCondition -notmatch "'\$\(Configuration\)'=='Release \(lite\)'" -or
    $null -eq $warningsAsErrors -or
    $warningsAsErrors.InnerText -ne 'true') {
    throw 'Release and Release (lite) must treat compiler warnings as errors.'
}

$updateHelper = Join-Path $projectDirectory 'UpdateHelper.cpp'
$updateHeader = Join-Path $projectDirectory 'UpdateHelper.h'
$forbiddenUpdatePattern = 'https?://|version_utf8\.info|gitee\.com/zhongyang219|github\.com/zhongyang219'
$forbiddenUpdateReferences = @(Select-String -LiteralPath @($updateHelper, $updateHeader) -Pattern $forbiddenUpdatePattern -AllMatches)
if ($forbiddenUpdateReferences.Count -gt 0) {
    throw 'UpdateHelper contains an upstream update URL or feed reference.'
}
$updateSource = Get-Content -LiteralPath $updateHelper -Raw
if ($updateSource -notmatch 'IsUpdateCheckSupported\(\) const noexcept\s*\{\s*return false;') {
    throw 'UpdateHelper must remain fail-closed until a signed project-owned manifest is implemented.'
}

$configSource = Get-Content -LiteralPath (Join-Path $projectDirectory 'core/ConfigStore.cpp') -Raw
if ($configSource -notmatch 'general\.check_update_when_start\s*=\s*false;') {
    throw 'ConfigStore must clear the legacy automatic-update opt-in.'
}
if ($configSource -notmatch '#ifdef WITHOUT_TEMPERATURE[\s\S]*general\.hardware_monitor_item\s*=\s*0;') {
    throw 'ConfigStore must clear imported hardware-monitor settings in temperature-free builds.'
}

$settingsSource = Get-Content -LiteralPath (Join-Path $projectDirectory 'GeneralSettingsDlg.cpp') -Raw
foreach ($control in @('IDC_CHECK_UPDATE_CHECK', 'IDC_CHECK_NOW_BUTTON', 'IDC_GITHUB_RADIO', 'IDC_GITEE_RADIO')) {
    if ($settingsSource -notmatch "EnableDlgCtrl\($control,\s*FALSE\)") {
        throw "General settings must disable the unavailable update control: $control."
    }
}

$forbiddenSourcePatterns = @(
    @{ Pattern = '\bCheckUpdateThreadFunc\b'; Description = 'unsafe background update UI entry point' },
    @{ Pattern = '\bTestCrash\b'; Description = 'intentional crash helper' }
)
$sourceFiles = @($projectXml.SelectNodes('//msbuild:ClCompile[@Include] | //msbuild:ClInclude[@Include]', $namespace) |
    ForEach-Object { Join-Path $projectDirectory $_.Include })
foreach ($forbidden in $forbiddenSourcePatterns) {
    $matches = @(Select-String -LiteralPath $sourceFiles -Pattern $forbidden.Pattern)
    if ($matches.Count -gt 0) {
        throw "Source contains $($forbidden.Description): $($matches[0].Path):$($matches[0].LineNumber)"
    }
}

Write-Host ("Project validation passed: {0} files, {1} hardened configurations, valid XML and filters." -f $projectItems.Count, $configurations.Count)
