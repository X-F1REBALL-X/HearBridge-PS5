param(
  [Parameter(Mandatory=$true)][string]$ElfPath,
  [string]$TargetHost = "<console-ip>",
  [int]$Port = 9021
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ElfPath)) { throw "ELF not found: $ElfPath" }
$bytes = [System.IO.File]::ReadAllBytes($ElfPath)
Write-Host ("Sending {0} bytes to {1}:{2}" -f $bytes.Length, $TargetHost, $Port)
$client = New-Object System.Net.Sockets.TcpClient
$client.Connect($TargetHost, $Port)
$stream = $client.GetStream()
$stream.Write($bytes, 0, $bytes.Length)
$stream.Flush()
Start-Sleep -Milliseconds 500
$stream.Close()
$client.Close()
Write-Host "OK sent"
