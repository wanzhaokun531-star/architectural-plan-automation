param(
    [Parameter(Mandatory)][string]$Source,
    [string]$Request,
    [Parameter(Mandatory)][string]$PriorReport,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [string]$WorkspaceRoot
)
$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot '../native/joint_annotation_repair.py'
$arguments = @('-B', '-X', 'utf8', $tool, '--source', $Source, '--prior-report', $PriorReport,
    '--output', $Output, '--work', $WorkDirectory)
if ($Request) { $arguments += @('--request', $Request) }
if ($WorkspaceRoot) { $arguments += @('--workspace-root', $WorkspaceRoot) }
& python @arguments
exit $LASTEXITCODE
