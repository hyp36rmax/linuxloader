param([Parameter(Mandatory=$true)][string]$SessionPath,[Parameter(Mandatory=$true)][int]$LoaderExitCode)
$ErrorActionPreference='Stop';$failures=[System.Collections.Generic.List[string]]::new()
foreach($name in @('driveboard_raw.aerbin','driveboard_raw.json','activation_diagnostics.json','native_activation.json','virtual_driveboard_status.json','vehicle_ffb_v2.csv')){if(-not(Test-Path -LiteralPath(Join-Path $SessionPath $name))){$failures.Add("missing output: $name")}}
if($failures.Count -eq 0){
  $native=Get-Content -Raw -LiteralPath(Join-Path $SessionPath 'native_activation.json')|ConvertFrom-Json
  $virtual=Get-Content -Raw -LiteralPath(Join-Path $SessionPath 'virtual_driveboard_status.json')|ConvertFrom-Json
  $first=Get-Content -LiteralPath(Join-Path $SessionPath 'vehicle_ffb_v2.csv') -TotalCount 1
  if($native.native_state.final_driver -ne 12){$failures.Add('native driver did not reach 12')}
  if($native.native_state.final_check -ne 2){$failures.Add('cabinet check did not reach 2')}
  if(-not $virtual.physical_isolation){$failures.Add('physical isolation failed')}
  if($first -ne '#schema=AER_VEHICLE_FFB_V2'){$failures.Add('vehicle telemetry schema mismatch')}
}
if($failures.Count -eq 0){@('AER-04 CAPTURE COMPLETE','Native ownership preserved.','Vehicle/FFB telemetry recorded.','Physical output isolated.',"Loader exit code: $LoaderExitCode")|Set-Content -LiteralPath(Join-Path $SessionPath 'AER04_SUCCESS.txt') -Encoding UTF8;exit 0}
@('AER-04 CAPTURE INCOMPLETE',"Loader exit code: $LoaderExitCode",'')+$failures|Set-Content -LiteralPath(Join-Path $SessionPath 'AER04_FAILURE.txt') -Encoding UTF8;exit 1
