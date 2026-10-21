param(
  [Parameter(Mandatory=$true)][string]$Porta,
  [string]$PastaSaida = ""
)

$ErrorActionPreference = "Stop"
if([string]::IsNullOrWhiteSpace($PastaSaida)) { $PastaSaida = Join-Path $PSScriptRoot "..\resultados" }
New-Item -ItemType Directory -Force -Path $PastaSaida | Out-Null
$serial = [System.IO.Ports.SerialPort]::new($Porta,115200,"None",8,"One")
$serial.NewLine = "`n"; $serial.ReadTimeout = 1000; $serial.DtrEnable = $true
$tecnica = "DESCONHECIDA"; $minimo=$null; $medio=$null; $maximo=$null; $linhas=@()

try {
  $serial.Open(); Start-Sleep -Milliseconds 1200; $serial.DiscardInBuffer(); $serial.WriteLine("r")
  Write-Host "TX iniciado na $Porta. Aguardando a rodada de 1000 quadros..."
  $limite = (Get-Date).AddSeconds(40)
  while((Get-Date) -lt $limite -and $null -eq $medio) {
    try { $linha=$serial.ReadLine().Trim() } catch [System.TimeoutException] { continue }
    if(-not $linha){continue}; $linhas += $linha; Write-Host $linha
    if($linha -match 'TX iniciou:\s*([^\.]+)'){ $tecnica=$Matches[1].Trim() }
    if($linha -match 'Calculo:\s*min=([0-9.,]+) us, medio=([0-9.,]+) us, max=([0-9.,]+) us'){
      $minimo=$Matches[1]; $medio=$Matches[2]; $maximo=$Matches[3]
    }
  }
  if($null -eq $medio){ throw "O resumo do TX nao apareceu em 40 segundos." }
  $carimbo=Get-Date -Format "yyyy-MM-dd_HH-mm-ss"; $nomeSeguro=$tecnica -replace '[^A-Za-z0-9_]','_'
  $txt=Join-Path $PastaSaida ("tx_{0}_{1}.txt" -f $nomeSeguro,$carimbo)
  $csv=Join-Path $PastaSaida ("tx_tempos_{0}_{1}.csv" -f $nomeSeguro,$carimbo)
  $linhas | Set-Content -LiteralPath $txt -Encoding utf8
  [pscustomobject]@{tecnica=$tecnica;quadros=1000;calculo_min_us=$minimo;calculo_medio_us=$medio;calculo_max_us=$maximo} |
    Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding utf8
  Write-Host "Log salvo: $txt"; Write-Host "Tempos salvos: $csv"
}
finally { if($serial.IsOpen){$serial.Close()}; $serial.Dispose() }
