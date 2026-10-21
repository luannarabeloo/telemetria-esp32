param([string]$PastaRx = "", [string]$PastaSaida = "")

$ErrorActionPreference = "Stop"
if([string]::IsNullOrWhiteSpace($PastaRx)) { $PastaRx = Join-Path $PSScriptRoot "..\dados\rx" }
if([string]::IsNullOrWhiteSpace($PastaSaida)) { $PastaSaida = Join-Path $PSScriptRoot "..\resultados" }
New-Item -ItemType Directory -Force -Path $PastaSaida | Out-Null
$arquivos = Get-ChildItem -LiteralPath $PastaRx -Filter "*.csv" -File
if(-not $arquivos) { throw "Nenhum CSV encontrado em $PastaRx" }

$todas = foreach($arquivo in $arquivos) {
  Import-Csv -LiteralPath $arquivo.FullName | ForEach-Object {
    $_ | Add-Member -NotePropertyName arquivo_origem -NotePropertyValue $arquivo.Name -PassThru
  }
}

$resumo = foreach($grupo in ($todas | Group-Object tecnica,modo)) {
  $itens = $grupo.Group
  $ok = @($itens | Where-Object status -eq "OK").Count
  $cor = @($itens | Where-Object status -eq "CORROMPIDO").Count
  $per = @($itens | Where-Object status -eq "PERDIDO").Count
  $nao = @($itens | Where-Object status -eq "NAO_DETECTADO").Count
  $den = $cor + $nao
  [pscustomobject]@{
    tecnica = $itens[0].tecnica
    modo = [int]$itens[0].modo
    OK = $ok
    CORROMPIDO = $cor
    PERDIDO = $per
    NAO_DETECTADO = $nao
    total = $itens.Count
    taxa_nao_deteccao_percentual = if($den){ [math]::Round(100*$nao/$den,4) } else { 0 }
  }
}

$resumo = $resumo | Sort-Object tecnica,modo
$resumoPath = Join-Path $PastaSaida "tabela_resultados.csv"
$resumo | Export-Csv -LiteralPath $resumoPath -NoTypeInformation -Encoding utf8

$exemplosPath = Join-Path $PastaSaida "casos_nao_detectados.csv"
$todas | Where-Object status -eq "NAO_DETECTADO" | Export-Csv -LiteralPath $exemplosPath -NoTypeInformation -Encoding utf8

$resumo | Format-Table -AutoSize
Write-Host "Tabela: $resumoPath"
Write-Host "Casos reais de nao deteccao: $exemplosPath"

