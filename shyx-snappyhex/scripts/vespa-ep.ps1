# Called by vespa ExternalProject. Uses VsDevCmd + clang-cl + Ninja.
param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("configure", "build", "install")]
  [string]$Step,
  [string]$Source = "",
  [string]$Binary = "",
  [string]$Prefix = "",
  [string]$FoamDir = "",
  [string]$Version = "2412",
  [string]$VsDev = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
  [string]$CMake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
  [string]$Ninja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
  [string]$Clang = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $VsDev)) { throw "VsDevCmd.bat not found: $VsDev" }
if (-not (Test-Path $CMake)) { throw "cmake.exe not found: $CMake" }

function Invoke-Dev([string]$Inner) {
  cmd.exe /c "`"$VsDev`" -arch=amd64 && $Inner"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Step) {
  "configure" {
    if (-not $Source -or -not $Binary -or -not $Prefix -or -not $FoamDir) {
      throw "configure requires -Source -Binary -Prefix -FoamDir"
    }
    if (-not (Test-Path $Clang)) { throw "clang-cl.exe not found: $Clang" }
    if (-not (Test-Path $Ninja)) { throw "ninja.exe not found: $Ninja" }
    $inner = "`"$CMake`" -G Ninja -S `"$Source`" -B `"$Binary`" " +
      "-DCMAKE_BUILD_TYPE=Release " +
      "`"-DCMAKE_MAKE_PROGRAM=$Ninja`" " +
      "`"-DCMAKE_CXX_COMPILER=$Clang`" " +
      "`"-DCMAKE_C_COMPILER=$Clang`" " +
      "-DCMAKE_INSTALL_PREFIX=`"$Prefix`" " +
      "-DFOAM_SOURCE_DIR=`"$FoamDir`" " +
      "-DSHYX_OPENFOAM_VERSION=$Version " +
      "-DSHYX_BUILD_OPENFOAM=ON -DSHYX_BUILD_CLI=OFF"
    Invoke-Dev $inner
  }
  "build" {
    if (-not $Binary) { throw "build requires -Binary" }
    Invoke-Dev "`"$CMake`" --build `"$Binary`" --parallel"
  }
  "install" {
    if (-not $Binary -or -not $Prefix) { throw "install requires -Binary -Prefix" }
    Invoke-Dev "`"$CMake`" --install `"$Binary`" --prefix `"$Prefix`""
  }
}
