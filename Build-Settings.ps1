param(
  [string]$MinGWCompiler,
  [switch]$SkipRestore
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
Push-Location $repo
try {
  New-Item -ItemType Directory -Force build/tools | Out-Null
  $nuget = Join-Path $repo 'build/tools/nuget.exe'
  if (!$SkipRestore) {
    if (!(Test-Path $nuget)) {
      Invoke-WebRequest 'https://dist.nuget.org/win-x86-commandline/v6.14.0/nuget.exe' -OutFile $nuget
    }
    & $nuget restore src/settings_ui/packages.config -PackagesDirectory build/ui-packages -NonInteractive -Source 'https://api.nuget.org/v3/index.json'
    if ($LASTEXITCODE) { throw 'NuGet restore failed.' }
  }
  $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
  $msbuild = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild/Current/Bin/amd64/MSBuild.exe'
  if (!$msbuild) { throw 'Visual Studio with MSVC v145 and Windows SDK 10.0.26100.0 is required.' }
  $configure = @('-S', '.', '-B', 'build', '-DALTSNAP_BUILD_TESTS=ON')
  if ($MinGWCompiler) { $configure += "-DCMAKE_CXX_COMPILER=$MinGWCompiler" }
  & cmake @configure
  if ($LASTEXITCODE) { throw 'Configure the MinGW core build first, or pass -MinGWCompiler <clang++.exe/g++.exe>.' }
  & cmake --build build
  if ($LASTEXITCODE) { throw 'Core build failed.' }
  & ctest --test-dir build --output-on-failure
  if ($LASTEXITCODE) { throw 'Core tests failed.' }
  foreach ($project in @('SettingsModelTests', 'AltSnap.Settings')) {
    & $msbuild "src/settings_ui/$project.vcxproj" /p:Configuration=Release /p:Platform=x64 /nr:false /v:minimal /nologo
    if ($LASTEXITCODE) { throw "$project build failed." }
  }
  Write-Output "Launch: $repo\build\stage\AltSnap.exe -winui"
  Write-Output 'Distribute the complete build\stage folder, including Settings and its runtime dependencies.'
} finally { Pop-Location }
