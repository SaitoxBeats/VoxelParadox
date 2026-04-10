param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path $scriptRoot
$buildDir = Join-Path $repoRoot 'build\msbuild'
$triplet = 'x64-windows-static-md'

New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

$initialPropsPath = Join-Path $buildDir 'VoxelParadox.Initial.props'
$settingsPropsPath = Join-Path $buildDir 'VoxelParadox.Settings.props'

$initialProps = @'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <VpRootDir>$([MSBuild]::EnsureTrailingSlash($([System.IO.Path]::GetFullPath('$(MSBuildThisFileDirectory)..\..\'))))</VpRootDir>
    <VpArtifactsDir>$(VpRootDir)artifacts\</VpArtifactsDir>
    <PreferredToolArchitecture>x64</PreferredToolArchitecture>
    <PlatformToolset>v145</PlatformToolset>
    <VcpkgEnabled>true</VcpkgEnabled>
    <VcpkgEnableManifest>true</VcpkgEnableManifest>
    <VcpkgManifestRoot>$(VpRootDir)</VcpkgManifestRoot>
    <VcpkgInstalledDir>$(VpRootDir)vcpkg_installed\</VcpkgInstalledDir>
    <VcpkgUseStatic>true</VcpkgUseStatic>
    <VcpkgUseMD>true</VcpkgUseMD>
    <VcpkgApplocalDeps>false</VcpkgApplocalDeps>
    <OutDir>$(VpArtifactsDir)bin\$(Platform)\$(Configuration)\$(MSBuildProjectName)\</OutDir>
    <IntDir>$(VpArtifactsDir)obj\$(Platform)\$(Configuration)\$(MSBuildProjectName)\</IntDir>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)'=='Debug'">
    <UseDebugLibraries>true</UseDebugLibraries>
    <LinkIncremental>true</LinkIncremental>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)'=='Release'">
    <UseDebugLibraries>false</UseDebugLibraries>
    <LinkIncremental>false</LinkIncremental>
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
</Project>
'@

$settingsProps = @'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <VpCommonPreprocessorDefinitions>WIN32;_WINDOWS;STB_IMAGE_STATIC;GLFW_INCLUDE_NONE</VpCommonPreprocessorDefinitions>
    <VpConfigPreprocessorDefinitions Condition="'$(Configuration)'=='Debug'">_DEBUG;VP_ENABLE_DEV_TOOLS</VpConfigPreprocessorDefinitions>
    <VpConfigPreprocessorDefinitions Condition="'$(Configuration)'=='Release'">NDEBUG;VP_ENABLE_DEV_TOOLS</VpConfigPreprocessorDefinitions>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(VpProjectIncludeDirs);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$(VpCommonPreprocessorDefinitions);$(VpConfigPreprocessorDefinitions);%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>/Zm1000 /utf-8 %(AdditionalOptions)</AdditionalOptions>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <ExceptionHandling>Sync</ExceptionHandling>
      <WarningLevel>Level3</WarningLevel>
      <ForcedIncludeFiles Condition="'$(VpPchHeader)' != ''">$(VpPchHeader);%(ForcedIncludeFiles)</ForcedIncludeFiles>
      <PrecompiledHeader Condition="'$(VpPchHeader)' != ''">Use</PrecompiledHeader>
      <PrecompiledHeaderFile Condition="'$(VpPchHeader)' != ''">$(VpPchHeader)</PrecompiledHeaderFile>
      <RuntimeLibrary Condition="'$(Configuration)'=='Debug'">MultiThreadedDebugDLL</RuntimeLibrary>
      <RuntimeLibrary Condition="'$(Configuration)'=='Release'">MultiThreadedDLL</RuntimeLibrary>
      <Optimization Condition="'$(Configuration)'=='Debug'">Disabled</Optimization>
      <Optimization Condition="'$(Configuration)'=='Release'">MaxSpeed</Optimization>
      <InlineFunctionExpansion Condition="'$(Configuration)'=='Release'">AnySuitable</InlineFunctionExpansion>
      <FunctionLevelLinking Condition="'$(Configuration)'=='Release'">true</FunctionLevelLinking>
      <IntrinsicFunctions Condition="'$(Configuration)'=='Release'">true</IntrinsicFunctions>
      <DebugInformationFormat Condition="'$(Configuration)'=='Debug'">ProgramDatabase</DebugInformationFormat>
      <DebugInformationFormat Condition="'$(Configuration)'=='Release'">ProgramDatabase</DebugInformationFormat>
    </ClCompile>
    <Link>
      <AdditionalDependencies>opengl32.lib;%(AdditionalDependencies)</AdditionalDependencies>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <SubSystem>Console</SubSystem>
      <OptimizeReferences Condition="'$(Configuration)'=='Release'">true</OptimizeReferences>
      <EnableCOMDATFolding Condition="'$(Configuration)'=='Release'">true</EnableCOMDATFolding>
    </Link>
  </ItemDefinitionGroup>
</Project>
'@

function Write-IfNeeded {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    if (-not $Force -and (Test-Path $Path)) {
        $current = Get-Content -Path $Path -Raw
        if ($current -eq $Content) {
            return
        }
    }

    Set-Content -Path $Path -Value $Content -Encoding UTF8
}

function Get-VcpkgExe {
    $command = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    if ($env:VCPKG_ROOT) {
        if ($env:VCPKG_ROOT.EndsWith('.exe')) {
            if (Test-Path $env:VCPKG_ROOT) {
                return $env:VCPKG_ROOT
            }
        } else {
            $candidate = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

function Restore-VcpkgManifest {
    $vcpkgExe = Get-VcpkgExe
    if ($null -eq $vcpkgExe) {
        Write-Host 'vcpkg.exe was not found in PATH or VCPKG_ROOT.'
        Write-Host "Skipping dependency restore for triplet '$triplet'."
        Write-Host 'If the solution still shows missing headers, install vcpkg or run this script from an environment where vcpkg is available.'
        return
    }

    $marker = Join-Path $repoRoot "vcpkg_installed\$triplet\include\glm\glm.hpp"
    if ((Test-Path $marker) -and -not $Force) {
        return
    }

    Push-Location $repoRoot
    try {
        & $vcpkgExe install --triplet $triplet
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}

Write-IfNeeded -Path $initialPropsPath -Content $initialProps
Write-IfNeeded -Path $settingsPropsPath -Content $settingsProps
Restore-VcpkgManifest

Write-Host "Bootstrap completed."
Write-Host "Created or updated:"
Write-Host " - $initialPropsPath"
Write-Host " - $settingsPropsPath"
