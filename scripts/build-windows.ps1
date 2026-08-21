[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion -lt [Version]'7.2.0') {
    throw 'PowerShell 7.2 or newer is required.'
}

foreach ($command in @('git', 'cmake')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command '$command' was not found in PATH."
    }
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = Join-Path $ProjectRoot '.build'
$Workspace = Join-Path $BuildRoot 'obs-plugintemplate'
$ReleaseRoot = Join-Path $ProjectRoot 'release'

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

if (-not (Test-Path (Join-Path $Workspace '.git'))) {
    Write-Host 'Cloning official OBS plugin template build infrastructure...'
    git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git $Workspace
} else {
    Write-Host 'Refreshing official OBS plugin template build infrastructure...'
    git -C $Workspace fetch origin master --depth 1
    git -C $Workspace reset --hard origin/master
}

Write-Host 'Preparing temporary build workspace...'

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'src')
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'data')
Copy-Item -Recurse -Force (Join-Path $ProjectRoot 'src') (Join-Path $Workspace 'src')
Copy-Item -Recurse -Force (Join-Path $ProjectRoot 'data') (Join-Path $Workspace 'data')

$buildSpecPath = Join-Path $Workspace 'buildspec.json'
$spec = Get-Content $buildSpecPath -Raw | ConvertFrom-Json
$spec.name = 'obs-virtual-audio'
$spec.displayName = 'OBS Virtual Audio'
$spec.version = '0.1.0'
$spec.author = 'Jiksi'
$spec.website = 'https://github.com/Jiksi/obs-virtual-audio'
$spec.email = 'noreply@example.com'

# Keep the plugin ABI and build dependencies aligned with the OBS release used
# for local smoke tests. The upstream plugin template currently targets OBS 31,
# which OBS 32 rejects as an incompatible module.
$spec.dependencies.'obs-studio'.version = '32.2.2'
$spec.dependencies.'obs-studio'.hashes.'windows-x64' =
    'f15f001f1fa526405318835f44f9910046502f496ebc3a30d5296a5018b831aa'
$spec.dependencies.prebuilt.version = '2026-07-15'
$spec.dependencies.prebuilt.hashes.'windows-x64' =
    '6f90e9598fa10cff5ad23cdcfae49b87868c07bf896b02cd464582b4ce2f2ba9'
$spec.dependencies.qt6.version = '2026-07-15'
$spec.dependencies.qt6.hashes.'windows-x64' =
    '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579'
$spec.dependencies.qt6.debugSymbols.'windows-x64' =
    '471d0b2191c424a520a51d064f3741084f117e1ff6ee5af1d16aabcdbacc6659'
$spec | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 $buildSpecPath

$presetsPath = Join-Path $Workspace 'CMakePresets.json'
$presets = Get-Content $presetsPath -Raw | ConvertFrom-Json
$windowsPreset = $presets.configurePresets | Where-Object { $_.name -eq 'windows-x64' }
if (-not $windowsPreset) {
    throw "The upstream template does not contain the expected 'windows-x64' preset."
}

$windowsPreset.generator = 'Visual Studio 18 2026'
$windowsPreset.architecture = 'x64,version=10.0.26100.0'
$presets | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 $presetsPath

# A cached CMake generator cannot be changed in place. This directory only
# contains the disposable template build tree; downloaded dependencies remain
# cached separately under .deps.
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'build_x64')

$cmake = @'
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/audio-capture.cpp
    src/audio-ring-buffer.cpp
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PLUGIN_VERSION="${_version}")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
'@
Set-Content -Encoding UTF8 (Join-Path $Workspace 'CMakeLists.txt') $cmake

# The upstream Windows build helper intentionally requires a CI marker.
$previousCI = $env:CI
$env:CI = '1'
try {
    & (Join-Path $Workspace '.github/scripts/Build-Windows.ps1') -Target x64 -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "OBS build helper failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:CI = $previousCI
}

$workspaceRelease = Join-Path $Workspace "release/$Configuration"
if (-not (Test-Path $workspaceRelease)) {
    throw "Build completed but release output was not found at $workspaceRelease"
}

$targetRelease = Join-Path $ReleaseRoot $Configuration
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $targetRelease
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
Copy-Item -Recurse -Force $workspaceRelease $targetRelease

Write-Host ''
Write-Host 'Build complete.'
Write-Host "Plugin package: $targetRelease"
Write-Host 'Look for obs-virtual-audio.dll under the obs-plugins/64bit directory.'
