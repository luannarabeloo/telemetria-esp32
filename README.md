# Telemetria UART com detecção de erros em ESP32

Projeto experimental de transmissão de telemetria entre ESP32-WROOM-32. O sistema compara checksum de 1 complemento e CRC-8 na presença de ruído físico controlado.

O experimento utiliza três placas:

- **TX:** monta e transmite os quadros;
- **RX:** verifica, classifica e registra os resultados;
- **NOISE:** observa o marcador inicial e injeta pulsos físicos na linha UART.

## Formato do quadro

```text
[0x7E] [SEQ] [LEN=2] [PAYLOAD0] [PAYLOAD1] [DETECCAO]
```

Depois da decisão do RX, o TX envia uma cópia experimental da verdade:

```text
[0xD5] [PAYLOAD0 verdadeiro] [PAYLOAD1 verdadeiro]
```

Essa cópia não participa da aceitação ou do descarte. Ela serve apenas para identificar erros não detectados depois que a decisão já foi tomada.

## Montagem

- GPIO 17 do TX → GPIO 16 do RX;
- GPIO 17 do TX → GPIO 16 do ESP32 de ruído;
- GND comum entre as três placas;
- GPIO 25 do ESP32 de ruído → resistor de 1 kΩ → base do transistor NPN;
- emissor do transistor → GND;
- coletor do transistor → linha TX/RX.

Parâmetros da UART: `115200 bit/s`, `8N1`.

## Escolha da técnica

Em `tx/tx.ino` e `rx/rx.ino`, use a mesma definição:

```cpp
#define DETECTION_MODE DETECTION_CHECKSUM
```

ou:

```cpp
#define DETECTION_MODE DETECTION_CRC8
```

O código de ruído não precisa ser recompilado ao trocar a técnica.

## Modos de ruído

| Modo | Comportamento |
|---:|---|
| 0 | Controle sem alteração programada |
| 1 | Um bit por quadro |
| 2 | Dois bits por quadro |
| 3 | Rajada de três a oito bits no mesmo byte |

## Resultados principais

| Técnica | Modo | OK | Corrompido | Perdido | Não detectado |
|---|---:|---:|---:|---:|---:|
| Checksum | 0 | 1000 | 0 | 0 | 0 |
| Checksum | 1 | 816 | 180 | 4 | 0 |
| Checksum | 2 | 651 | 343 | 5 | 1 |
| Checksum | 3 | 404 | 589 | 7 | 0 |
| CRC-8 | 1 | 807 | 189 | 4 | 0 |
| CRC-8 | 2 | 655 | 342 | 3 | 0 |
| CRC-8 | 3 | 399 | 584 | 16 | 1 |

As tabelas completas, tempos e evidências estão em `resultados/`. A análise dos casos não detectados e do timeout está em `docs/ANALISE_DAS_EVIDENCIAS.md`.

## Estrutura

```text
tx/                 código do transmissor
rx/                 código do receptor
noise/              código do injetor físico
dados/              CSVs separados por técnica e modo
resultados/         tabelas e evidências consolidadas
docs/               especificação e análise
ferramentas/        scripts PowerShell de captura
```

## Coleta dos CSVs

Feche os Monitores Seriais do RX e do ruído antes de executar os scripts. Exemplo para modo 1:

```powershell
.\ferramentas\capturar_ruido.ps1 -Porta COM5 -Modo 1
.\ferramentas\capturar_rx.ps1 -Porta COM6 -Modo 1
```

Depois de ambos estarem armados, envie `r` ao TX. Cada script salva 1000 registros.

Para gerar novamente as tabelas:

```powershell
.\ferramentas\gerar_tabelas.ps1
```

## Observação sobre reprodutibilidade

O ruído é físico e suas posições são pseudoaleatórias. Uma tentativa pode ocorrer quando a linha já está baixa ou pode alterar a amostragem UART de forma diferente entre rodadas. Portanto os valores exatos variam, embora as tendências possam ser comparadas.

