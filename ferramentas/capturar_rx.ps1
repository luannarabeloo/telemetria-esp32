param(
  [Parameter(Mandatory=$true)][string]$Porta,
  [ValidateRange(0,3)][int]$Modo,
  [string]$PastaSaida = ""
)

$ErrorActionPreference = "Stop"
if([string]::IsNullOrWhiteSpace($PastaSaida)) { $PastaSaida = Join-Path $PSScriptRoot "..\dados\rx" }
New-Item -ItemType Directory -Force -Path $PastaSaida | Out-Null
$carimbo = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$arquivo = Join-Path $PastaSaida ("rx_modo_{0}_{1}.csv" -f $Modo,$carimbo)
$cabecalho = "tecnica,modo,slot,seq_esperada,seq_rx,len,p0_rx,p1_rx,det_rx,residuo,decisao,p0_real,p1_real,payload_confere,status,estado_seq,lacuna_seq,tempo_verificacao_us,tempo_recuperacao_us,timestamp_us"

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
  Write-Host "RX armado na $Porta, modo $Modo. Agora inicie o TX."
  Write-Host "Arquivo sendo gravado: $arquivo"

  $linhas = 0
  while($linhas -lt 1000) {
    try { $linha = $serial.ReadLine().Trim() }
    catch [System.TimeoutException] { continue }
    if($linha -match '^(CHECKSUM_1_COMPLEMENTO|CRC8_POLY_07),[0-3],[0-9]+,') {
      Add-Content -LiteralPath $arquivo -Value $linha -Encoding utf8
      $linhas++
      if(($linhas % 100) -eq 0) { Write-Host "$linhas/1000 quadros salvos" }
    }
  }
  $dados = Import-Csv -LiteralPath $arquivo
  $qtdOk = @($dados | Where-Object status -eq "OK").Count
  $qtdCorrompido = @($dados | Where-Object status -eq "CORROMPIDO").Count
  $qtdPerdido = @($dados | Where-Object status -eq "PERDIDO").Count
  $qtdNaoDetectado = @($dados | Where-Object status -eq "NAO_DETECTADO").Count
  $denominador = $qtdCorrompido + $qtdNaoDetectado
  $taxa = if($denominador) { [math]::Round(100*$qtdNaoDetectado/$denominador,4) } else { 0 }
  $resumo = @(
    "RESUMO CALCULADO A PARTIR DO CSV",
    "Modo: $Modo",
    "OK: $qtdOk",
    "CORROMPIDO: $qtdCorrompido",
    "PERDIDO: $qtdPerdido",
    "NAO_DETECTADO: $qtdNaoDetectado",
    "Total: $($dados.Count)",
    "Taxa de nao deteccao: $taxa %"
  )
  $resumoPath = [System.IO.Path]::Combine([System.IO.Path]::GetDirectoryName($arquivo),([System.IO.Path]::GetFileNameWithoutExtension($arquivo)+"_resumo.txt"))
  $resumo | Set-Content -LiteralPath $resumoPath -Encoding utf8
  Write-Host ""
  $resumo | ForEach-Object { Write-Host $_ }
  Write-Host "CSV concluido: $arquivo"
  Write-Host "Resumo salvo: $resumoPath"
}
finally {
  if($serial.IsOpen) { $serial.Close() }
  $serial.Dispose()
}

