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
$spec | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 $buildSpecPath

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
