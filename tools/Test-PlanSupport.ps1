#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path ([IO.Path]::GetTempPath()) 'PlanSupportTests'),
    [string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildBase = [IO.Path]::GetFullPath($BuildRoot)
$runName = 'run-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runDirectory = Join-Path $buildBase $runName
[void][IO.Directory]::CreateDirectory($runDirectory)
$receiptPath = Join-Path $runDirectory 'result.json'
$utf8 = New-Object Text.UTF8Encoding($false)
$steps = New-Object 'Collections.Generic.List[object]'
$inputs = New-Object 'Collections.Generic.List[object]'
$started = [DateTime]::UtcNow
$failure = $null
$overallPassed = $false
$resolvedMSBuild = $null
$runtimeBefore = $null
$runtimeAfter = $null
$runtimeSnapshotTaken = $false
$runtimePath = Join-Path $repository 'skills/architectural-plan-automation/tools/PlanNative.arx'

function Find-MSBuild {
    if ($MSBuildPath) {
        if (!(Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) { throw "MSBuild not found: $MSBuildPath" }
        return [IO.Path]::GetFullPath($MSBuildPath)
    }
    $onPath = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $installer = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $installer 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $matches = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
        foreach ($candidate in $matches) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw 'Install Visual Studio MSBuild, v141 x64 C++ tools and Windows SDK 10.0.19041.0, or pass -MSBuildPath.'
}

function Quote-NativeArgument([string]$Value) {
    # Windows CommandLineToArgvW-compatible quoting; no shell interpretation.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    return '"' + [regex]::Replace($escaped, '(\\+)$', '$1$1') + '"'
}

function Invoke-TestStep([string]$Name, [string]$Executable, [string[]]$ArgumentList) {
    $stdoutPath = Join-Path $runDirectory ($Name + '.stdout.log')
    $stderrPath = Join-Path $runDirectory ($Name + '.stderr.log')
    $stepStarted = [DateTime]::UtcNow
    $stepError = $null
    $exitCode = $null
    $process = New-Object Diagnostics.Process
    try {
        $info = New-Object Diagnostics.ProcessStartInfo
        $info.FileName = $Executable
        $info.Arguments = ($ArgumentList | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
        $info.WorkingDirectory = $repository
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $process.StartInfo = $info
        if (!$process.Start()) { throw "Process did not start: $Executable" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        [IO.File]::WriteAllText($stdoutPath, $stdout.GetAwaiter().GetResult(), $utf8)
        [IO.File]::WriteAllText($stderrPath, $stderr.GetAwaiter().GetResult(), $utf8)
    } catch {
        $stepError = $_.Exception.Message
    } finally {
        $process.Dispose()
    }
    $record = [ordered]@{
        name = $Name; executable = $Executable; arguments = @($ArgumentList)
        startedUtc = $stepStarted.ToString('o')
        durationSeconds = [Math]::Round(([DateTime]::UtcNow - $stepStarted).TotalSeconds, 3)
        exitCode = $exitCode; passed = ($null -eq $stepError -and $exitCode -eq 0)
        stdout = $stdoutPath; stderr = $stderrPath; error = $stepError
    }
    $steps.Add([pscustomobject]$record)
    Write-Host ("{0}: {1} (exit {2})" -f $Name, $(if ($record.passed) { 'PASS' } else { 'FAIL' }), $exitCode)
    return [bool]$record.passed
}

try {
    if (Test-Path -LiteralPath $runtimePath -PathType Leaf) { $runtimeBefore = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash }
    $runtimeSnapshotTaken = $true
    $resolvedMSBuild = Find-MSBuild
    $specifications = @(
        @{ name = 'DiagnosticTests'; project = '插件/tests/DiagnosticTests.vcxproj' },
        @{ name = 'PolicyTests'; project = '插件/tests/PolicyTests.vcxproj' },
        @{ name = 'ProjectInputTests'; project = '插件/tests/ProjectInputTests.vcxproj' },
        @{ name = 'ApronGeometryTests'; project = '阶段2/tests/ApronGeometryTests.vcxproj' }
    )
    foreach ($specification in $specifications) {
        $projectPath = Join-Path $repository $specification.project
        if (!(Test-Path -LiteralPath $projectPath -PathType Leaf)) { throw "Missing test project: $projectPath" }
        [xml]$project = [IO.File]::ReadAllText($projectPath)
        $types = @($project.SelectNodes("//*[local-name()='ConfigurationType']") | ForEach-Object { $_.InnerText })
        if (!$types.Count -or @($types | Where-Object { $_ -ne 'Application' }).Count) {
            throw "Test project must build only a console application: $projectPath"
        }
        $inputs.Add([pscustomobject]@{ path = $specification.project; sha256 = (Get-FileHash -LiteralPath $projectPath -Algorithm SHA256).Hash })
        foreach ($node in $project.SelectNodes("//*[local-name()='ClCompile' and @Include]")) {
            $source = [IO.Path]::GetFullPath((Join-Path (Split-Path $projectPath -Parent) $node.Include))
            $inputs.Add([pscustomobject]@{ path = $source.Substring($repository.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash })
        }
    }
    $hostTest = Join-Path $repository '阶段1/原生接口探测/tests/TestPostExitWindowPolicy.ps1'
    foreach ($path in @($hostTest, (Join-Path $repository '阶段1/原生接口探测/启动隔离桌面宿主.ps1'), $PSCommandPath)) {
        $inputs.Add([pscustomobject]@{ path = $path.Substring($repository.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash })
    }
    foreach ($specification in $specifications) {
        $testName = $specification.name
        $output = Join-Path $runDirectory ($testName + '/bin')
        $intermediate = Join-Path $runDirectory ($testName + '/obj')
        [void][IO.Directory]::CreateDirectory($output)
        [void][IO.Directory]::CreateDirectory($intermediate)
        $buildArguments = @((Join-Path $repository $specification.project), '/nologo', '/m:1', '/nr:false', '/v:minimal', '/p:Configuration=Release', '/p:Platform=x64', ('/p:OutDir=' + $output.Replace('\', '/') + '/'), ('/p:IntDir=' + $intermediate.Replace('\', '/') + '/'), ('/p:TargetName=' + $testName))
        $built = Invoke-TestStep ($testName + '-build') $resolvedMSBuild $buildArguments
        if ($built) {
            $testArguments = @()
            if ($testName -eq 'PolicyTests') {
                $scratch = Join-Path $runDirectory 'policy-empty-scratch'
                [void][IO.Directory]::CreateDirectory($scratch)
                $testArguments = @($scratch)
            }
            [void](Invoke-TestStep ($testName + '-run') (Join-Path $output ($testName + '.exe')) $testArguments)
        }
    }
    $powershell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    [void](Invoke-TestStep 'PostExitWindowPolicy-run' $powershell @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $hostTest))
    $overallPassed = ($steps.Count -eq 9 -and @($steps | Where-Object { !$_.passed }).Count -eq 0)
} catch {
    $failure = $_.Exception.Message
} finally {
    if (Test-Path -LiteralPath $runtimePath -PathType Leaf) { $runtimeAfter = (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash }
    if ($runtimeSnapshotTaken -and $runtimeBefore -ne $runtimeAfter) { $failure = 'Production PlanNative.arx changed during the test run.'; $overallPassed = $false }
    $receipt = [ordered]@{
        schemaVersion = 1; kind = 'plan-support-pure-tests'; repository = $repository
        startedUtc = $started.ToString('o'); finishedUtc = [DateTime]::UtcNow.ToString('o')
        runDirectory = $runDirectory; msbuild = $resolvedMSBuild; passed = $overallPassed
        error = $failure; cadStarted = $false; desktopControlled = $false
        scope = 'Pure C++ support algorithms and eight extracted host-policy cases; no DWG generation, CAD run, or all-rule acceptance.'
        productionRuntimeBeforeSHA256 = $runtimeBefore; productionRuntimeAfterSHA256 = $runtimeAfter
        inputs = @($inputs.ToArray()); steps = @($steps.ToArray())
    }
    [IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 10), $utf8)
    Write-Host "Receipt: $receiptPath"
}
if (!$overallPassed) { if ($failure) { Write-Host "FAIL: $failure" }; exit 1 }
exit 0
