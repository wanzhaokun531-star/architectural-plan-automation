param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Output,
    [string]$WorkspaceRoot,
    [string]$WorkDirectory,
    [string]$ProjectParameters,
    [string]$PlotStyle,
    [string]$Module,
    [string]$TemplateSheets,
    [ValidateSet('off', 'audit', 'repair')][string]$PostcheckMode
)
$ErrorActionPreference = 'Stop'
$entry = Join-Path $PSScriptRoot '../native/unattended_plan.py'
$machine = Join-Path $PSScriptRoot '../host/machine.json'
$python = 'python'
if (Test-Path -LiteralPath $machine -PathType Leaf) {
    $python = (Get-Content -LiteralPath $machine -Raw -Encoding UTF8 | ConvertFrom-Json).python
}
$arguments = @('-B', '-X', 'utf8', $entry, '--source', $Source, '--output', $Output)
foreach ($pair in @(
    @{ Name = '--workspace-root'; Value = $WorkspaceRoot },
    @{ Name = '--work-directory'; Value = $WorkDirectory },
    @{ Name = '--project-parameters'; Value = $ProjectParameters },
    @{ Name = '--plot-style'; Value = $PlotStyle },
    @{ Name = '--module'; Value = $Module },
    @{ Name = '--template-sheets'; Value = $TemplateSheets },
    @{ Name = '--postcheck-mode'; Value = $PostcheckMode }
)) {
    if ($pair.Value) { $arguments += @($pair.Name, $pair.Value) }
}
& $python @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
