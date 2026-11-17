param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Sheets,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][string]$WorkspaceRoot
)
$ErrorActionPreference = 'Stop'
& python -B -X utf8 (Join-Path $PSScriptRoot '../native/template_revision.py') --source $Source --sheets $Sheets --work $WorkDirectory --workspace-root $WorkspaceRoot
exit $LASTEXITCODE
