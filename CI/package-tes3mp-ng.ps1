# Packages a Windows build of TES3MP-NG into release archives:
#   TES3MP-NG-<version>-win64.zip               client, launcher, wizard, server browser, dedicated server, CoreScripts-NG
#   TES3MP-NG-MasterServer-<version>-win64.zip  master server, for hosting your own server list
#   TES3MP-NG-<version>-win64-symbols.zip       PDB files, for reading crash dumps
#   SHA256SUMS.txt
#
# Build first (RelWithDebInfo, like OpenMW's own Windows releases):
#   cmake --build MSVC2026_64 --config RelWithDebInfo --target openmw tes3mp-server tes3mp-browser masterserver
#       openmw-launcher openmw-wizard openmw-iniimporter openmw-navmeshtool
#
#   .\CI\package-tes3mp-ng.ps1 -CoreScripts ..\CoreScripts -CrabNet ..\CrabNet
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\MSVC2026_64"),
    [string]$Config = "RelWithDebInfo",
    [Parameter(Mandatory = $true)][string]$CoreScripts,
    [Parameter(Mandatory = $true)][string]$CrabNet,
    [string]$CoreScriptsRef = "HEAD",
    [string]$OutDir = (Join-Path $PSScriptRoot "..\release")
)
$ErrorActionPreference = "Stop"

$source = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$bin = (Resolve-Path (Join-Path $BuildDir $Config)).Path
$CoreScripts = (Resolve-Path $CoreScripts).Path
$CrabNet = (Resolve-Path $CrabNet).Path

$versionHeader = Get-Content (Join-Path $source "components\openmw-mp\Version.hpp") -Raw
if ($versionHeader -notmatch '#define TES3MP_VERSION "([^"]+)"') { throw "TES3MP_VERSION not found in Version.hpp" }
$version = $Matches[1]
$commit = (git -C $source rev-parse --short=10 HEAD).Trim()
$dirty = git -C $source status --porcelain --untracked-files=no
if ($dirty) { Write-Warning "The source tree has uncommitted changes: the release won't match commit $commit" }

$clientName = "TES3MP-NG-$version-win64"
$masterName = "TES3MP-NG-MasterServer-$version-win64"
$symbolsName = "TES3MP-NG-$version-win64-symbols"
$staging = Join-Path $OutDir "staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force $staging | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

function Copy-Required([string]$from, [string]$to) {
    if (-not (Test-Path $from)) { throw "Missing $from (is the build complete?)" }
    $parent = Split-Path -Parent $to
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force $parent | Out-Null }
    Copy-Item -Recurse -Force $from $to
}

# Visual C++ runtime, app-local, so players don't need to install the redistributable
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: is Visual Studio installed?" }
$vs = (& $vswhere -latest -products * -property installationPath).Trim()
$crt = Get-ChildItem "$vs\VC\Redist\MSVC" -Directory | ForEach-Object { Join-Path $_.FullName "x64" } |
    Where-Object { Test-Path $_ } | ForEach-Object { Get-ChildItem $_ -Directory } |
    Where-Object { $_.Name -like "Microsoft.VC*.CRT" } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw "Visual C++ runtime redistributable (VC\Redist\MSVC\<version>\x64\Microsoft.VC*.CRT) not found" }

$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
    Where-Object { Test-Path $_ } | ForEach-Object { Get-Item $_ } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw "dumpbin.exe not found" }

function Get-Dependencies([string[]]$executables) {
    # DLLs from the build folder that the executables load, directly or through other DLLs
    $found = @{}
    $queue = New-Object System.Collections.Queue
    foreach ($exe in $executables) { $queue.Enqueue($exe) }
    while ($queue.Count -gt 0) {
        $file = $queue.Dequeue()
        foreach ($line in (& $dumpbin.FullName /dependents $file)) {
            $name = $line.Trim()
            if ($name -notmatch '\.dll$') { continue }
            $path = Join-Path $bin $name
            if ((Test-Path $path) -and -not $found.ContainsKey($name.ToLower())) {
                $found[$name.ToLower()] = $path
                $queue.Enqueue($path)
            }
        }
    }
    return $found.Values
}

function Add-Licenses([string]$to) {
    Copy-Required (Join-Path $source "LICENSE") (Join-Path $to "LICENSE.txt")
    Copy-Required (Join-Path $source "README.md") (Join-Path $to "README.txt")
    Copy-Required (Join-Path $source "AUTHORS.md") (Join-Path $to "AUTHORS.txt")
    Copy-Required (Join-Path $source "tes3mp-credits.md") (Join-Path $to "tes3mp-credits.txt")
    Copy-Required (Join-Path $CrabNet "LICENSE") (Join-Path $to "licenses\CrabNet-LICENSE.txt")
    Copy-Required (Join-Path $CrabNet "PATENTS") (Join-Path $to "licenses\CrabNet-PATENTS.txt")
}

function Add-SourceNote([string]$to) {
    @(
        "TES3MP-NG $version, built from commit $commit",
        "Source code: https://github.com/kyaustad/TES3MP-NG/tree/$commit",
        "CoreScripts-NG: https://github.com/kyaustad/CoreScripts-NG",
        "CrabNet: https://github.com/TES3MP/CrabNet"
    ) | Set-Content -Encoding utf8 (Join-Path $to "SOURCE.txt")
}

# ---------------------------------------------------------------------------------------------------------------------
# Client and dedicated server
$client = Join-Path $staging $clientName
New-Item -ItemType Directory -Force $client | Out-Null

$clientExes = "openmw", "openmw-launcher", "openmw-wizard", "openmw-iniimporter", "openmw-navmeshtool",
    "tes3mp-browser", "tes3mp-server"
foreach ($exe in $clientExes) { Copy-Required (Join-Path $bin "$exe.exe") (Join-Path $client "$exe.exe") }

# Every DLL and plugin folder the build deploys, like OpenMW's own install rules
Get-ChildItem -Path $bin -Filter *.dll | ForEach-Object { Copy-Item $_.FullName $client }
foreach ($dir in "iconengines", "imageformats", "platforms", "styles", "osgPlugins-*") {
    Get-ChildItem -Path $bin -Directory -Filter $dir | ForEach-Object { Copy-Required $_.FullName (Join-Path $client $_.Name) }
}
Copy-Item (Join-Path $crt.FullName "*.dll") $client

Copy-Required (Join-Path $bin "resources") (Join-Path $client "resources")
Copy-Required (Join-Path $bin "defaults.bin") (Join-Path $client "defaults.bin")
Copy-Required (Join-Path $bin "gamecontrollerdb.txt") (Join-Path $client "gamecontrollerdb.txt")
Copy-Required (Join-Path $bin "openmw.cfg.install") (Join-Path $client "openmw.cfg")

# Default TES3MP configs from the source tree, not the build folder, where tests may have changed them
Copy-Required (Join-Path $source "files\tes3mp\tes3mp-client-default.cfg") (Join-Path $client "tes3mp-client-default.cfg")
Copy-Required (Join-Path $source "files\tes3mp\tes3mp-server-default.cfg") (Join-Path $client "tes3mp-server-default.cfg")

# CoreScripts-NG as committed (git archive leaves out untracked files such as server data from tests)
$serverDir = Join-Path $client "server"
$archive = Join-Path $staging "corescripts.zip"
git -C $CoreScripts archive --format=zip -o $archive $CoreScriptsRef
if ($LASTEXITCODE -ne 0) { throw "git archive of CoreScripts failed" }
Expand-Archive -Path $archive -DestinationPath $serverDir
Remove-Item $archive

Copy-Required (Join-Path $source "docs\tes3mp") (Join-Path $client "docs")
Add-Licenses $client
Add-SourceNote $client

# ---------------------------------------------------------------------------------------------------------------------
# Master server
$master = Join-Path $staging $masterName
New-Item -ItemType Directory -Force $master | Out-Null
Copy-Required (Join-Path $bin "masterserver.exe") (Join-Path $master "masterserver.exe")
foreach ($dll in (Get-Dependencies @((Join-Path $bin "masterserver.exe")))) { Copy-Item $dll $master }
Copy-Item (Join-Path $crt.FullName "*.dll") $master
Add-Licenses $master
Add-SourceNote $master
@(
    "TES3MP-NG master server $version",
    "",
    "Keeps the list of servers that the server browser shows.",
    "",
    "    masterserver.exe [master port] [HTTP port]",
    "",
    "The ports default to 25560 (servers and browsers, UDP) and 8080 (read-only HTTP API: /api/servers,",
    "/api/servers/<address>:<port>, /api/servers/info, TCP). Open them in your firewall.",
    "",
    "Servers report to it with these settings in tes3mp-server-default.cfg:",
    "    [MasterServer]",
    "    enabled = true",
    "    address = <your master server's address>",
    "    port = 25560",
    "",
    "Players' server browsers use it with these settings in tes3mp-client-default.cfg (or tes3mp-client.cfg):",
    "    [Master]",
    "    address = <your master server's address>",
    "    port = 25560"
) | Set-Content -Encoding utf8 (Join-Path $master "MASTER-SERVER.txt")

# ---------------------------------------------------------------------------------------------------------------------
# Symbols
$symbols = Join-Path $staging $symbolsName
New-Item -ItemType Directory -Force $symbols | Out-Null
foreach ($exe in $clientExes + "masterserver") {
    $pdb = Join-Path $bin "$exe.pdb"
    if (Test-Path $pdb) { Copy-Item $pdb $symbols }
}

# ---------------------------------------------------------------------------------------------------------------------
$outputs = @()
foreach ($name in $clientName, $masterName, $symbolsName) {
    $zip = Join-Path $OutDir "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    & "$env:SystemRoot\System32\tar.exe" -a -c -f $zip -C $staging $name
    if ($LASTEXITCODE -ne 0) { throw "Creating $zip failed" }
    $outputs += $zip
}
Remove-Item -Recurse -Force $staging

$sums = foreach ($zip in $outputs) {
    "$((Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower())  $(Split-Path -Leaf $zip)"
}
$sums | Set-Content -Encoding ascii (Join-Path $OutDir "SHA256SUMS.txt")

foreach ($zip in $outputs) { "{0,8:N1} MB  {1}" -f ((Get-Item $zip).Length / 1MB), $zip }
"Commit $commit, CoreScripts-NG $((git -C $CoreScripts rev-parse --short=10 $CoreScriptsRef).Trim())"
