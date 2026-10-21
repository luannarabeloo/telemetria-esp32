param(
  [Parameter(Mandatory=$true)][string]$Porta,
  [ValidateRange(0,3)][int]$Modo,
  [string]$PastaSaida = ""
)

$ErrorActionPreference = "Stop"
if([string]::IsNullOrWhiteSpace($PastaSaida)) { $PastaSaida = Join-Path $PSScriptRoot "..\dados\ruido" }
New-Item -ItemType Directory -Force -Path $PastaSaida | Out-Null
$carimbo = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$arquivo = Join-Path $PastaSaida ("ruido_modo_{0}_{1}.csv" -f $Modo,$carimbo)
$cabecalho = "quadro,modo,qtd_bits,posicoes,mudou_mask,timestamp_marcador_us"

$serial = [System.IO.Ports.SerialPort]::new($Porta,115200,"None",8,"One")
$serial.NewLine = "`n"
$serial.ReadTimeout = 30000
$serial.DtrEnable = $true

try {
  $serial.Open()
  Start-Sleep -Milliseconds 1200
  $serial.DiscardInBuffer()
  $serial.WriteLine($Modo.ToString())
  Start-Sleep -Milliseconds 100
  $serial.WriteLine("r")
  $cabecalho | Set-Content -LiteralPath $arquivo -Encoding utf8
  Write-Host "Ruido armado na $Porta, modo $Modo. Depois arme o RX e inicie o TX."
  Write-Host "Arquivo sendo gravado: $arquivo"

  $linhas = 0
  while($linhas -lt 1000) {
    try { $linha = $serial.ReadLine().Trim() }
    catch [System.TimeoutException] { continue }
    if($linha -match '^[0-9]+,[0-3],[0-9]+,') {
      Add-Content -LiteralPath $arquivo -Value $linha -Encoding utf8
      $linhas++
      if(($linhas % 100) -eq 0) { Write-Host "$linhas/1000 eventos salvos" }
    }
  }
  Write-Host "Concluido: $arquivo"
}
finally {
  if($serial.IsOpen) { $serial.Close() }
  $serial.Dispose()
}

