param(
    [string]$AcadPath,
    [string]$TianzhengKernel,
    [string]$Template,
    [string]$BaseProfile,
    [string]$PythonPath,
    [string[]]$FontDirectory,
    [switch]$InstallPythonDependencies,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required.' }

$root = Split-Path -Parent $PSScriptRoot
$hostRoot = Join-Path $root 'host'
$manifest = Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($entry in $manifest.PSObject.Properties) {
    if ($entry.Name -match '(^[\\/]|(^|[\\/])\.\.([\\/]|$)|:)') { throw "Invalid package path: $($entry.Name)" }
    $file = Join-Path $root $entry.Name
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $entry.Value) {
        throw "Package integrity check failed: $($entry.Name)"
    }
}

$python = if ($PythonPath) { (Get-Item -LiteralPath $PythonPath).FullName }
          else { (Get-Command python.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source }
& $python -c 'import sys; assert sys.version_info >= (3,11) and sys.maxsize > 2**32'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or later (x64) is required.' }
if ($InstallPythonDependencies -and !$CheckOnly) {
    & $python -m pip install PyMuPDF Pillow fontTools
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install Python printing dependencies.' }
}
& $python -c 'import pymupdf, PIL, fontTools'
if ($LASTEXITCODE -ne 0) { throw 'Python needs PyMuPDF, Pillow and fontTools. Run again with -InstallPythonDependencies.' }

$acadRegistry = 'SOFTWARE\Autodesk\AutoCAD\R23.1'
$products = @(Get-ChildItem -LiteralPath "HKLM:\$acadRegistry" -ErrorAction SilentlyContinue)
$installations = @(foreach ($product in $products) {
    $location = $product.GetValue('AcadLocation')
    if ($location) {
        $candidate = Join-Path $location 'acad.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            [pscustomobject]@{ exe = (Get-Item -LiteralPath $candidate).FullName; key = $product.PSChildName }
        }
    }
})
if ($AcadPath) { $AcadPath = (Get-Item -LiteralPath $AcadPath).FullName }
else {
    $executables = @($installations.exe | Select-Object -Unique)
    if ($executables.Count -ne 1) { throw 'Specify the AutoCAD 2020 executable with -AcadPath.' }
    $AcadPath = $executables[0]
}
$acad = Get-Item -LiteralPath $AcadPath
if ($acad.Name -ine 'acad.exe' -or $acad.VersionInfo.ProductMajorPart -ne 23 -or $acad.VersionInfo.ProductMinorPart -ne 1) {
    throw 'The selected executable is not AutoCAD 2020 R23.1.'
}

$profiles = @(foreach ($product in $installations) {
    if ($product.exe -ine $AcadPath) { continue }
    $key = "HKCU:\$acadRegistry\$($product.key)\Profiles"
    foreach ($profile in @(Get-ChildItem -LiteralPath $key -ErrorAction SilentlyContinue)) {
        if ($profile.PSChildName -notlike 'CodexS1Native_*') {
            [pscustomobject]@{ path = $profile.PSPath; key = $key; name = $profile.PSChildName;
                              preferred = $profile.PSChildName -eq (Get-Item -LiteralPath $key).GetValue('') }
        }
    }
})
if ($BaseProfile) { $profiles = @($profiles | Where-Object name -EQ $BaseProfile) }
else {
    $preferred = @($profiles | Where-Object preferred)
    $ordinary = @($profiles | Where-Object { $_.name -match '^<<.+>>$' -and
        (Test-Path -LiteralPath (Join-Path $_.path 'General') -PathType Container) })
    $localized = @($ordinary | Where-Object { $_.key -like '*:804\Profiles' -and $_.name -ceq '<<未命名配置>>' })
    if ($localized.Count -eq 1) { $profiles = $localized }
    elseif ($ordinary.Count -eq 1) { $profiles = $ordinary }
    elseif ($preferred.Count -eq 1) { $profiles = $preferred }
}
if (!$profiles.Count) { throw 'Open AutoCAD 2020 once to initialize a local profile, or specify -BaseProfile.' }
if ($profiles.Count -gt 1) { throw ('Select a base AutoCAD profile with -BaseProfile. Found: ' + ($profiles.name -join ', ')) }
$base = $profiles[0]

if (!$TianzhengKernel) {
    $vendorHomes = @((Join-Path ($env:SystemDrive + '\') 'Tangent'), (Join-Path $env:ProgramFiles 'Tangent'))
    $kernels = @(foreach ($homePath in $vendorHomes) {
        Get-ChildItem -LiteralPath $homePath -Filter tch_kernal.arx -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.Directory.Name -ieq 'SYS23x64' } | Select-Object -ExpandProperty FullName
    })
    if ($kernels.Count -ne 1) { throw 'Specify the compatible Tianzheng SYS23x64/tch_kernal.arx with -TianzhengKernel.' }
    $TianzhengKernel = $kernels[0]
}
$kernel = Get-Item -LiteralPath $TianzhengKernel
if ($kernel.Name -ine 'tch_kernal.arx') { throw 'A Tianzheng tch_kernal.arx component is required.' }

if (!$Template) {
    $templates = @(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'Autodesk/AutoCAD 2020/R23.1') `
        -Filter acadiso.dwt -File -Recurse -ErrorAction SilentlyContinue)
    if (!$templates.Count) { throw 'Specify the installed acadiso.dwt template with -Template.' }
    $Template = $templates[0].FullName
}
$Template = (Get-Item -LiteralPath $Template).FullName

$general = Get-Item -LiteralPath (Join-Path $base.path 'General')
$support = @(($general.GetValue('ACAD', '') -split ';') | Where-Object { $_ })
$vendorSupport = @($kernel.DirectoryName, (Join-Path $kernel.Directory.Parent.FullName 'SYS')) |
                 Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$fontPaths = @($support) + @($vendorSupport) + @(Join-Path $acad.DirectoryName 'Fonts') + @($FontDirectory)
$fontPaths = @($fontPaths | Where-Object { $_ } | ForEach-Object { [Environment]::ExpandEnvironmentVariables($_.Trim('"')) })
$resolvedFonts = @{}
$missing = @(foreach ($font in @('simplex.shx', 'gbcbig.shx', 'hztxt.shx', 'tssdeng.shx')) {
    $folder = $fontPaths | Where-Object { Test-Path -LiteralPath (Join-Path $_ $font) -PathType Leaf } | Select-Object -First 1
    if ($folder) { $resolvedFonts[$font] = Join-Path $folder $font }
    else { $font }
})
if ($missing.Count) { throw ('Install the following CAD/Tianzheng fonts in the support paths: ' + ($missing -join ', ')) }

$result = [ordered]@{acadPath=$AcadPath; template=$Template; supportComponent=$kernel.FullName; python=$python; fonts=$resolvedFonts}
if (!$CheckOnly) {
    $profile = 'CodexS1Native_' + [Guid]::NewGuid().ToString('N').Substring(0,12)
    $target = Join-Path $base.key $profile
    $stagingRoot = Join-Path $env:ProgramFiles ('Autodesk/ApplicationPlugins/CodexPlanDraw_' + $profile)
    $runs = Join-Path $hostRoot 'runs'
    $fontFolder = Join-Path $hostRoot 'local-fonts'
    try { [void][IO.Directory]::CreateDirectory($stagingRoot) }
    catch [UnauthorizedAccessException] { throw 'Run the install script once in an elevated PowerShell window under the same Windows account.' }
    $userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = Get-Acl -LiteralPath $stagingRoot
    $access = [Security.AccessControl.FileSystemAccessRule]::new(
        $userSid, 'Modify', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
    $acl.SetAccessRule($access)
    Set-Acl -LiteralPath $stagingRoot -AclObject $acl
    [void][IO.Directory]::CreateDirectory($runs)
    [void][IO.Directory]::CreateDirectory($fontFolder)
    foreach ($font in $resolvedFonts.Keys) {
        Copy-Item -LiteralPath $resolvedFonts[$font] -Destination (Join-Path $fontFolder $font) -Force
    }
    Copy-Item -LiteralPath $base.path -Destination $target -Recurse
    $variables = Join-Path $target 'Variables'
    $trusted = @(((Get-Item -LiteralPath $variables).GetValue('TRUSTEDPATHS', '') -split ';') | Where-Object { $_ })
    $trusted += @($kernel.DirectoryName, ($stagingRoot + '\...'), ($runs + '\...'))
    Set-ItemProperty -LiteralPath $variables -Name TRUSTEDPATHS -Value (($trusted | Select-Object -Unique) -join ';')
    Set-ItemProperty -LiteralPath $variables -Name SECURELOAD -Value '1'
    Set-ItemProperty -LiteralPath (Join-Path $target 'General') -Name ACAD -Value ((@($support) + @($kernel.DirectoryName, $fontFolder) | Select-Object -Unique) -join ';')
    $result['profile'] = $profile
    $result['profileRegistry'] = $base.key.Substring('HKCU:\'.Length)
    $result['stagingRoot'] = $stagingRoot
    $configuration = Join-Path $hostRoot 'machine.json'
    $temporary = Join-Path $hostRoot "$profile.tmp"
    [IO.File]::WriteAllText($temporary, ($result | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $configuration -Force
}
$result | ConvertTo-Json
