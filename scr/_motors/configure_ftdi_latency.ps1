[CmdletBinding()]
param(
    [ValidatePattern('^COM[0-9]+$')]
    [string]$PortName = 'COM3',

    [ValidateRange(1, 255)]
    [int]$LatencyMilliseconds = 1,

    [switch]$NoRestart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
    return $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )
}

$MatchingPorts = @(
    Get-PnpDevice -Class Ports -PresentOnly |
        Where-Object {
            $_.FriendlyName -match
                "\($([regex]::Escape($PortName))\)$"
        }
)

if ($MatchingPorts.Count -ne 1) {
    throw "Expected one present device for $PortName; found $($MatchingPorts.Count)."
}

$Device = $MatchingPorts[0]
if ($Device.InstanceId -notlike 'FTDIBUS\*') {
    throw "$PortName is not an FTDI VCP device. No setting was changed."
}

$DeviceParameters =
    "HKLM:\SYSTEM\CurrentControlSet\Enum\$($Device.InstanceId)\Device Parameters"
$CurrentLatency =
    (Get-ItemProperty -LiteralPath $DeviceParameters -Name LatencyTimer).LatencyTimer

if ($CurrentLatency -eq $LatencyMilliseconds) {
    Write-Host (
        "$PortName FTDI latency is already $LatencyMilliseconds ms."
    ) -ForegroundColor Green
    return
}

if (-not (Test-IsAdministrator)) {
    $Arguments =
        "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" " +
        "-PortName $PortName -LatencyMilliseconds $LatencyMilliseconds"
    if ($NoRestart) {
        $Arguments += ' -NoRestart'
    }
    $Elevated = Start-Process `
        -FilePath 'powershell.exe' `
        -ArgumentList $Arguments `
        -Verb RunAs `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($Elevated.ExitCode -ne 0) {
        throw "Elevated FTDI configuration failed with exit code $($Elevated.ExitCode)."
    }
    return
}

Set-ItemProperty `
    -LiteralPath $DeviceParameters `
    -Name LatencyTimer `
    -Type DWord `
    -Value $LatencyMilliseconds

if (-not $NoRestart) {
    & pnputil.exe /restart-device $Device.InstanceId
    if ($LASTEXITCODE -ne 0) {
        throw "The latency value was set, but the device restart failed."
    }
}

$AppliedLatency =
    (Get-ItemProperty -LiteralPath $DeviceParameters -Name LatencyTimer).LatencyTimer
Write-Host (
    "$PortName FTDI latency changed from $CurrentLatency ms to $AppliedLatency ms."
) -ForegroundColor Green
