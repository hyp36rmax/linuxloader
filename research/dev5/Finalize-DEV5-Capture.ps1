param(
    [Parameter(Mandatory = $true)][string]$SessionPath,
    [Parameter(Mandatory = $true)][int]$LoaderExitCode
)

$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()
$required = @(
    'driveboard_raw.aerbin',
    'driveboard_raw.json',
    'activation_diagnostics.json',
    'native_activation.json',
    'virtual_driveboard_status.json'
)

foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $SessionPath $name))) {
        $failures.Add("missing diagnostic output: $name")
    }
}

if ($failures.Count -eq 0) {
    $native = Get-Content -Raw -LiteralPath (Join-Path $SessionPath 'native_activation.json') | ConvertFrom-Json
    $virtual = Get-Content -Raw -LiteralPath (Join-Path $SessionPath 'virtual_driveboard_status.json') | ConvertFrom-Json
    $raw = Get-Content -Raw -LiteralPath (Join-Path $SessionPath 'driveboard_raw.json') | ConvertFrom-Json

    if (-not $native.hooks.complete) { $failures.Add('native observer hooks were incomplete') }
    if ($native.native_state.final_driver -ne 12) { $failures.Add("native driver state was $($native.native_state.final_driver), expected 12") }
    if ($native.native_state.final_check -ne 2) { $failures.Add("native check state was $($native.native_state.final_check), expected 2") }
    foreach ($name in @('CabinetCtrl_Main','DrCtrlDataSet','DrCtrlMoveSend','steerReqSendOut','hardcomSend')) {
        $entry = $native.pipeline | Where-Object { $_.name -eq $name }
        if ($null -eq $entry -or $entry.count -lt 1) { $failures.Add("native pipeline did not observe $name") }
    }
    if (-not $virtual.eligible) { $failures.Add('virtual drive-board eligibility failed') }
    if (-not $virtual.physical_isolation) { $failures.Add('physical-output isolation failed') }
    if ($virtual.native_command_frames -lt 1) { $failures.Add('no native command frames reached virtual SERIAL0') }
    if ($raw.schema -ne 'AER_DRIVEBOARD_RAW_V1') { $failures.Add('raw recorder schema mismatch') }
}

if ($failures.Count -eq 0) {
    @(
        'DEV 5 SUCCESS',
        'Native driver state 12 observed.',
        'Native cabinet check state 2 observed.',
        'Original steering command pipeline produced outbound commands.',
        'Physical motor/serial output remained isolated.',
        "Loader exit code: $LoaderExitCode"
    ) | Set-Content -LiteralPath (Join-Path $SessionPath 'DEV5_SUCCESS.txt') -Encoding UTF8
    exit 0
}

@('DEV 5 INCOMPLETE', "Loader exit code: $LoaderExitCode", '') + $failures |
    Set-Content -LiteralPath (Join-Path $SessionPath 'DEV5_FAILURE.txt') -Encoding UTF8
exit 1
