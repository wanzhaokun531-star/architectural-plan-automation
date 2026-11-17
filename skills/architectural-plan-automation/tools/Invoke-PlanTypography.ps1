param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Sheets,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [string]$WorkspaceRoot,
    [string]$FontDirectory
)
$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot '../native/typography_revision.py'
$arguments = @('-B', '-X', 'utf8', $tool, '--source', $Source, '--sheets', $Sheets, '--work', $WorkDirectory)
if ($WorkspaceRoot) { $arguments += @('--workspace-root', $WorkspaceRoot) }
if ($FontDirectory) { $arguments += @('--font-directory', $FontDirectory) }
& python @arguments
exit $LASTEXITCODE
