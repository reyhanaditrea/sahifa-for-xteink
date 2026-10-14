# Builds and runs the host gtest suites on Windows.
#
# MSVC's compiler needs INCLUDE/LIB from vcvars64.bat, and the PlatformIO
# toolchain packages are the only cmake/ninja on this machine, so both are
# imported here rather than expected on PATH. Linux/CI run cmake directly
# (see .github/workflows/ci.yml) and never need this script.
param([string]$Target = "", [string]$TestFilter = "")

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
  if ($_ -match '^(INCLUDE|LIB|LIBPATH|PATH)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
$env:PATH = "$env:USERPROFILE\.platformio\packages\tool-cmake\bin;$env:USERPROFILE\.platformio\packages\tool-ninja;$env:PATH"

$repo = Split-Path -Parent $PSScriptRoot
cmake -S "$repo\test" -B "$repo\build\test" -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Target) { cmake --build "$repo\build\test" --target $Target } else { cmake --build "$repo\build\test" }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($TestFilter) { ctest --test-dir "$repo\build\test" --output-on-failure -j -R $TestFilter }
else { ctest --test-dir "$repo\build\test" --output-on-failure -j }
exit $LASTEXITCODE
