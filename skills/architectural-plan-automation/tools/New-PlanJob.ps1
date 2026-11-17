param(
    [Parameter(Mandatory)][string]$Source,
    [string]$Output,
    [Parameter(Mandatory)][string]$ReportDirectory,
    [ValidateSet('Generate','Revise','Audit')][string]$Mode = 'Revise',
    [ValidateRange(0.01,10000)][double]$Scale,
    [ValidateRange(0.001,1000000)][double]$UnitMM,
    [string]$NewDimensions,
    [string]$ConfirmedRooms,
    [string]$EntranceSteps,
    [string]$IndoorStairs,
    [string]$PlanDetailConfirmations,
    [string]$FurnitureComponents,
    [string]$RoofPlan,
    [string]$FloorLevels,
    [string]$PanelScope,
    [string]$SheetChecks,
    [string]$RoomNameReference,
    [string]$ProjectParameters,
    [switch]$PrintReview,
    [switch]$Formal,
    [string]$ResumeFrom,
    [switch]$RoomTextConversion,
    [string]$PlotStyle,
    [string]$PreviousPlanLedger,
    [string]$Resources,
    [string]$WorkspaceRoot
)
$ErrorActionPreference = 'Stop'
if (-not $Resources) { $Resources = Join-Path $PSScriptRoot '../assets' }
$sourceFile = (Resolve-Path -LiteralPath $Source).Path
$report = [IO.Path]::GetFullPath($ReportDirectory)
$resourceRoot = (Resolve-Path -LiteralPath $Resources).Path
$outputFile = ''
$resumeDrawing = ''
$resumeHash = ''
if ($ResumeFrom) {
    if ($Mode -ne 'Revise' -or -not $Formal) {
        throw 'ResumeFrom requires Revise with Formal and the original saved source DWG.'
    }
    $prior = (Resolve-Path -LiteralPath $ResumeFrom).Path
    $receipt = Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $prior '生成校验.json') | ConvertFrom-Json
    $ledgerFile = (Resolve-Path -LiteralPath (Join-Path $prior 'plan-rule-ledger.json')).Path
    $ledger = Get-Content -Raw -Encoding utf8 -LiteralPath $ledgerFile | ConvertFrom-Json
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile).Hash
    if ($receipt.contract -ne 'bcp.product-result.v1' -or -not $receipt.completed -or -not $receipt.published -or
        $receipt.sourceSHA256 -ne $sourceHash -or $ledger.sourceSHA256 -ne $sourceHash -or
        $ledger.outputSHA256 -ne $receipt.outputSHA256) {
        throw 'The completed prior output, source and rule ledger do not agree.'
    }
    $resumeDrawing = (Resolve-Path -LiteralPath $receipt.output).Path
    $resumeHash = (Get-FileHash -LiteralPath $resumeDrawing).Hash
    if ($resumeHash -ne $receipt.outputSHA256 -or $resumeDrawing -eq $sourceFile) {
        throw 'The prior result was edited or replaced. Preserve those edits and use the regular Revise path.'
    }
    if ($PreviousPlanLedger -and (Resolve-Path -LiteralPath $PreviousPlanLedger).Path -ne $ledgerFile) {
        throw 'PreviousPlanLedger differs from the bound revision receipt.'
    }
    $PreviousPlanLedger = $ledgerFile
}
if ($Mode -ne 'Audit') {
    if (-not $Output) { throw 'Supply an output DWG path.' }
    $outputFile = [IO.Path]::GetFullPath($Output)
    if ($sourceFile -eq $outputFile -or (Test-Path -LiteralPath $outputFile)) {
        throw 'Select a new output DWG path.'
    }
    if (-not (Test-Path -LiteralPath (Split-Path -Parent $outputFile) -PathType Container)) {
        throw 'Create the output parent folder before preparing a job.'
    }
}
if ($Mode -ne 'Audit' -and ($Scale -le 0 -or $UnitMM -le 0)) {
    throw 'Supply both Scale and UnitMM from the drawing.'
}
if (($Mode -eq 'Generate' -or ($Mode -eq 'Revise' -and $Formal)) -and
    ($Scale -ne 100 -or $UnitMM -ne 1)) {
    throw "Full-plan generation currently supports 1:100 model millimeters. This job specifies 1:$Scale and $UnitMM mm per drawing unit."
}
if (Test-Path -LiteralPath $report) {
    throw 'Select a new report directory for this job.'
}
if (-not $WorkspaceRoot -and -not $PSBoundParameters.ContainsKey('Resources')) {
    $current = Get-Item -LiteralPath (Get-Location).Path
    while ($current -and -not $WorkspaceRoot) {
        if ((Test-Path -LiteralPath (Join-Path $current.FullName '插件/tools/sync_plan_policy.py')) -and
            (Test-Path -LiteralPath (Join-Path $current.FullName 'docs/规则汇编_20261105/建筑施工图平面自动化通用规则_20261105.json'))) {
            $WorkspaceRoot = $current.FullName
        }
        $current = $current.Parent
    }
}
if ($WorkspaceRoot) {
    $workspace = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    $main = Join-Path $workspace 'docs/规则汇编_20261105/建筑施工图平面自动化通用规则_20261105.json'
    $packaged = Join-Path $resourceRoot 'plan-policy/all-plan-rules.json'
    if ((Get-FileHash -LiteralPath $main).Hash -ne (Get-FileHash -LiteralPath $packaged).Hash) {
        & python -B -X utf8 (Join-Path $workspace '插件/tools/sync_plan_policy.py') --sync-skills | Out-Host
        if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $main).Hash -ne (Get-FileHash -LiteralPath $packaged).Hash) {
            throw 'The selected rule resources do not match the current master compilation.'
        }
    }
}
$settings = [ordered]@{
    mode = $Mode.ToLowerInvariant()
    source = $sourceFile
    output = $outputFile
    report = $report
    resources = $resourceRoot
    scale = $Scale.ToString([Globalization.CultureInfo]::InvariantCulture)
    unitMM = $UnitMM.ToString([Globalization.CultureInfo]::InvariantCulture)
}
if ($NewDimensions) {
    $settings.newDimensions = (Resolve-Path -LiteralPath $NewDimensions).Path
}
if ($RoomTextConversion) { $settings.roomTextConversion = '1' }
if ($ConfirmedRooms) {
    $settings.confirmedRooms = (Resolve-Path -LiteralPath $ConfirmedRooms).Path
}
if ($EntranceSteps) {
    $settings.entranceSteps = (Resolve-Path -LiteralPath $EntranceSteps).Path
}
if ($IndoorStairs) {
    $settings.indoorStairs = (Resolve-Path -LiteralPath $IndoorStairs).Path
}
if ($PlanDetailConfirmations) {
    $settings.planDetailConfirmations = (Resolve-Path -LiteralPath $PlanDetailConfirmations).Path
}
if ($FurnitureComponents) {
    $settings.furnitureComponents = (Resolve-Path -LiteralPath $FurnitureComponents).Path
}
if ($RoofPlan) {
    $settings.roofPlan = (Resolve-Path -LiteralPath $RoofPlan).Path
}
if ($FloorLevels) {
    $settings.floorLevels = (Resolve-Path -LiteralPath $FloorLevels).Path
}
if ($PanelScope) { $settings.panelScope = (Resolve-Path -LiteralPath $PanelScope).Path }
if ($SheetChecks) {
    if ($Mode -ne 'Generate' -and -not $ResumeFrom) { throw 'Sheet checks require Generate or a formal revision from a prior receipt.' }
    $settings.sheetChecks = (Resolve-Path -LiteralPath $SheetChecks).Path
}
if ($RoomNameReference) {
    if ($Mode -ne 'Generate' -and -not $ResumeFrom) { throw 'Room name reference requires Generate or a formal revision from a prior receipt.' }
    $settings.roomNameReference = (Resolve-Path -LiteralPath $RoomNameReference).Path
}
if ($ProjectParameters) { $settings.projectParameters = (Resolve-Path -LiteralPath $ProjectParameters).Path }
if ($PrintReview) { $settings.printReview = '1' }
if ($Formal) {
    if ($Mode -ne 'Generate' -and -not $resumeDrawing) {
        throw 'Formal revision requires ResumeFrom and the original saved source DWG.'
    }
    $settings.delivery = 'formal'
    if ($resumeDrawing) {
        $settings.resumePreviousDWG = $resumeDrawing
        $settings.resumePreviousSHA256 = $resumeHash
    }
}
if ($PlotStyle) { $settings.plotStyle = $PlotStyle }
if ($PreviousPlanLedger) {
    $settings.previousPlanLedger = (Resolve-Path -LiteralPath $PreviousPlanLedger).Path
}
foreach ($value in $settings.Values) {
    if ($value -match '[\r\n]') { throw 'Job values must be one line.' }
}
[void](New-Item -Path $report -ItemType Directory)
$job = Join-Path $report 'plan.ini'
$lines = @('[plan]')
foreach ($entry in $settings.GetEnumerator()) {
    $lines += '{0}={1}' -f $entry.Key,$entry.Value
}
[IO.File]::WriteAllLines($job,$lines,[Text.Encoding]::Unicode)
Write-Output $job
