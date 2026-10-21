# Especificacao do protocolo de telemetria UART

## 1. Objetivo

O sistema usa tres ESP32-WROOM-32:

- **TX:** monta e transmite os quadros de telemetria.
- **RX:** recebe, verifica, aceita ou descarta cada quadro e registra os resultados.
- **RUIDO:** monitora a transmissao e, conforme o modo selecionado, puxa a linha para nivel baixo para provocar erros reais.

O enlace e unidirecional: `TX -> RX`. Nao existem ACK, NACK nem retransmissao. Depois de transmitir um quadro, o TX segue para o proximo.

## 2. Interface fisica

- Placa selecionada na Arduino IDE: **ESP32 Dev Module**.
- UART utilizada: **UART2**.
- Formato UART: **115200 baud, 8 bits, sem paridade e 1 stop bit (8N1)**.
- Tempo nominal de um bit: `1 / 115200 = 8,68 us`.
- Tempo nominal de um byte UART completo: `10 x 8,68 = 86,8 us`.
- TX: `GPIO 17 (TX2)`.
- RX: `GPIO 16 (RX2)`.
- Controle do transistor no ESP32 de ruido: `GPIO 25`.
- Todos os ESP32 devem compartilhar o **GND**.
- O injetor usa resistor de `1 kohm` e transistor NPN 2N2222, com o coletor ligado a linha TX->RX e o emissor ao GND. A pinagem do transistor deve ser conferida no datasheet do componente usado.

## 3. Formato do quadro principal

Cada quadro principal tem seis bytes:

```text
+--------+--------+--------+----------+----------+-----------+
| INICIO |  SEQ   |  LEN   | PAYLOAD0 | PAYLOAD1 | DETECCAO  |
+--------+--------+--------+----------+----------+-----------+
|  0x7E  | 1 byte | 1 byte |  1 byte  |  1 byte  |  1 byte   |
+--------+--------+--------+----------+----------+-----------+
```

| Posicao | Campo | Valor/tamanho | Funcao | Protegido pelo algoritmo? |
|---:|---|---|---|---|
| 0 | `INICIO` | `0x7E` | Permite ao RX localizar o inicio de um quadro | Nao |
| 1 | `SEQ` | 1 byte | Numero de sequencia, de 0 a 255 | Sim |
| 2 | `LEN` | `0x02` | Quantidade de bytes do payload | Sim |
| 3 | `PAYLOAD0` | 1 byte | Primeiro dado de telemetria | Sim |
| 4 | `PAYLOAD1` | 1 byte | Segundo dado de telemetria | Sim |
| 5 | `DETECCAO` | 1 byte | Checksum-8 ou CRC-8, conforme a rodada | Entra na verificacao do residuo |

O payload tem exatamente dois bytes, que e o minimo pedido. Os outros quatro bytes nao fazem parte do payload. Portanto:

- tamanho total do quadro principal: **6 bytes**;
- dados uteis: **2 bytes**;
- overhead do protocolo: **4 bytes** (`INICIO`, `SEQ`, `LEN` e `DETECCAO`).

O marcador completo e `01111110` em binario, isto e, `0x7E`. A sequencia `011111` isolada nao e o marcador escolhido.

## 4. Delimitacao e framing

Foi escolhido um **marcador somente para o inicio** e um **campo de comprimento para determinar o fim**. Nao existe marcador de fim.

O RX faz o seguinte:

1. Enquanto esta ocioso, procura `0x7E`.
2. Ao encontra-lo, le `SEQ` e `LEN`.
3. Para este experimento, aceita como estruturalmente valido somente `LEN = 2`.
4. Le dois bytes de payload e um byte de deteccao.
5. Executa a verificacao e toma a decisao antes de consultar a copia de depuracao.

Como o tamanho determina exatamente quantos bytes devem ser lidos, `0x7E` pode aparecer dentro do payload sem iniciar outro quadro. O RX so interpreta `0x7E` como inicio quando esta no estado de procura. Por isso nao sera usado byte stuffing nem bit stuffing.

Se `LEN` for diferente de 2 ou o quadro nao terminar dentro do timeout, o quadro e considerado incompleto. O RX abandona os bytes daquele quadro, espera o intervalo ocioso e volta a procurar `0x7E`.

## 5. Payload de teste

Durante os experimentos, o TX gera:

```text
PAYLOAD0 = 0xFF
PAYLOAD1 = SEQ
```

O primeiro byte com todos os bits em 1 aumenta a chance de o transistor realmente alterar um bit, pois o circuito fisico consegue forcar apenas a transicao `1 -> 0`. O segundo byte variavel simula uma medida de telemetria e tambem facilita relacionar o quadro com seu numero de sequencia.

## 6. Numero de sequencia

`SEQ` possui 8 bits e segue a ordem:

```text
0, 1, 2, ..., 254, 255, 0, 1, ...
```

O retorno de 255 para 0 e normal e usa aritmetica modulo 256.

O numero de sequencia nao detecta sozinho corrupcao de bits. Sua funcao principal e perceber lacunas entre quadros. A corrupcao e detectada pelo checksum ou CRC, e o campo `SEQ` tambem entra no calculo desse algoritmo.

Depois de um quadro aceito, o RX espera `(SEQ_anterior + 1) mod 256`:

- valor esperado: ordem normal;
- valor adiante do esperado: existe uma lacuna e os numeros saltados sao registrados como perdidos;
- valor repetido ou para tras: registrar `DUPLICADO_FORA_DE_ORDEM` e nao entregar novamente o dado a aplicacao;
- `255 -> 0`: wrap-around normal, nao e perda.

Para eliminar ambiguidade na comparacao modular, assume-se que nao ocorrera uma sequencia de 128 ou mais quadros consecutivos perdidos. A classificacao experimental usa tambem a copia verdadeira de depuracao, mas somente depois de a decisao do RX estar travada.

## 7. Protocolo 1: checksum-8 em complemento de 1

Na primeira rodada, o campo `DETECCAO` e um checksum de 8 bits com soma em complemento de 1.

Bytes usados no calculo do TX, nesta ordem:

```text
SEQ, LEN, PAYLOAD0, PAYLOAD1
```

Regra do TX:

1. Somar os quatro bytes.
2. Sempre que houver carry acima de 8 bits, somar esse carry de volta aos 8 bits inferiores.
3. Inverter os oito bits do resultado.
4. Anexar esse valor como `DETECCAO`.

Regra do RX:

1. Somar `SEQ`, `LEN`, `PAYLOAD0`, `PAYLOAD1` e o proprio `DETECCAO` com a mesma regra de carry circular.
2. O residuo esperado de um quadro intacto e `0xFF`.
3. Residuo `0xFF`: checagem passou.
4. Qualquer outro residuo: checagem falhou e o quadro e descartado.

## 8. Protocolo 2: CRC-8

Na segunda rodada, somente o algoritmo de deteccao muda. O formato, o payload, os tempos, a sequencia e o tamanho do quadro permanecem iguais.

Parametros definidos para evitar ambiguidades entre implementacoes:

```text
Nome: CRC-8
Polinomio: 0x07  (x^8 + x^2 + x + 1)
Valor inicial: 0x00
RefIn: false
RefOut: false
XorOut: 0x00
Residuo esperado ao verificar dados + CRC: 0x00
```

Bytes protegidos, na mesma ordem:

```text
SEQ, LEN, PAYLOAD0, PAYLOAD1
```

O TX calcula o CRC desses quatro bytes e o anexa. O RX processa os quatro bytes e o campo CRC recebido; o quadro passa somente se o residuo final for `0x00`.

Checksum e CRC nunca sao usados simultaneamente no mesmo quadro. Uma constante de compilacao igual nos codigos do TX e RX selecionara a tecnica da rodada.

## 9. Temporizacao definida

Os valores iniciais do protocolo sao:

| Constante | Valor | Motivo |
|---|---:|---|
| Periodo entre inicios de quadros | `20 ms` | Separa claramente os 1000 eventos e deixa tempo para log |
| Pausa depois de `0x7E` | `2 ms` | Da tempo ao ESP32 de ruido para detectar o marcador e armar o timer |
| Timeout interno do quadro | `5 ms` a partir de `0x7E` | Um quadro normal termina em cerca de 2,52 ms, mas um quadro malformado termina antes da copia verdadeira |
| Pausa entre o quadro e a copia de depuracao | `4 ms` | Garante que o RX ja decidiu e que um quadro incompleto expirou antes da copia |
| Tolerancia de inicio | `10 ms` | Um inicio ainda ausente 10 ms depois do instante esperado e declarado perdido |

No fio, os seis bytes ocupam aproximadamente `6 x 86,8 us = 521 us`. Incluindo a pausa de 2 ms depois do marcador, um quadro normal termina aproximadamente 2,52 ms depois de seu inicio.

Depois da primeira sincronizacao, o RX espera um novo marcador a cada 20 ms. Se ele nao aparecer ate 10 ms depois do instante previsto, o slot e marcado como perdido e o instante esperado avanca mais 20 ms. Os valores definitivos devem ser conferidos com as medicoes do analisador logico e registrados no relatorio.

## 10. Copia de depuracao

Depois de decidir aceitar ou descartar o quadro principal, o TX envia fora do quadro protegido:

```text
[0xD5] [PAYLOAD0_VERDADEIRO] [PAYLOAD1_VERDADEIRO]
```

Essa copia:

- nao entra no checksum ou CRC;
- nao conta como overhead do protocolo;
- nao pode ser atingida pelo gerador de ruido;
- nunca e usada para o RX decidir se aceita ou descarta o quadro;
- e lida somente depois da decisao, para descobrir se um quadro aceito tinha payload correto ou errado.

O marcador `0xD5` so e reconhecido como depuracao depois do intervalo ocioso que sucede o quadro principal. Isso evita confundi-lo com um byte que esteja dentro do quadro.

## 11. Decisao e classificacao no RX

Primeiro o RX toma uma decisao usando exclusivamente o quadro recebido:

- residuo esperado: `ACEITAR`;
- residuo diferente: `DESCARTAR`.

Quando a copia verdadeira chega, o RX fecha uma unica classificacao para aquele slot:

| Situacao | Status final |
|---|---|
| Quadro completo, checagem passou e payload recebido igual ao verdadeiro | `OK` |
| Quadro completo, checagem falhou | `CORROMPIDO` |
| RX nao conseguiu concluir a checagem: marcador ausente, comprimento invalido ou timeout | `PERDIDO` |
| Quadro completo, checagem passou, mas payload recebido difere do verdadeiro | `NAO_DETECTADO` |

Um quadro ja contado como `CORROMPIDO` nao pode ser contado novamente como `PERDIDO`. Cada rodada de 1000 quadros precisa resultar exatamente em 1000 classificacoes.

Observacao: na tabela do enunciado, a descricao de `OK` aparentemente contem um erro de digitacao ao dizer que a checagem "indicou erro". Para ser coerente com o restante do enunciado, `OK` significa que a checagem **nao indicou erro** e o payload estava correto.

Taxa calculada:

```text
taxa de nao deteccao = NAO_DETECTADO / (CORROMPIDO + NAO_DETECTADO)
```

Essa taxa considera apenas os quadros em que o ruido realmente alterou algum bit.

## 12. Ressincronizacao e recuperacao

- Depois de `CORROMPIDO`, o RX descarta o quadro, nao entrega seu payload e aguarda o proximo inicio.
- Depois de comprimento invalido ou timeout, o RX ignora o restante do slot, espera a regiao ociosa e volta ao estado de procura por `0x7E`.
- Nao existe retransmissao; o TX continua com o proximo numero.
- O tempo de recuperacao sera medido desde o instante em que a perda e declarada ate a conclusao e classificacao do proximo quadro completo.
- A copia de depuracao pode associar o resultado ao slot experimental, mas nunca pode mudar a decisao que ja foi tomada sobre o quadro principal.

## 13. Gerador de ruido

O modo e escolhido pelo Monitor Serial do ESP32 de ruido, sem recompilar:

| Modo | Acao em cada quadro |
|---:|---|
| 0 | Nao injeta ruido |
| 1 | Escolhe uma posicao de bit aleatoria e tenta forca-la para 0 |
| 2 | Escolhe duas posicoes distintas e tenta forca-las para 0 |
| 3 | Escolhe um byte e uma faixa contigua aleatoria de 3 a 8 bits dentro desse mesmo byte |

Depois de receber `0x7E`, o ESP32 de ruido escolhe apenas bits de dados dos cinco bytes seguintes: `SEQ`, `LEN`, `PAYLOAD0`, `PAYLOAD1` e `DETECCAO`. A copia de depuracao fica fora da janela de injecao.

Os bits UART sao transmitidos do menos significativo para o mais significativo. O pulso deve atingir o centro do bit escolhido. O codigo usara `esp_timer` ou timer de hardware; nao usara `delayMicroseconds()` para programar a injecao.

O transistor so consegue transformar `1` em `0`. Se o bit escolhido ja for 0, nao existe mudanca efetiva. Por isso, para cada tentativa, o log do ESP32 de ruido deve registrar:

- numero/slot do quadro;
- modo;
- indice do byte e indice do bit escolhidos;
- tamanho da rajada, quando aplicavel;
- timestamp;
- nivel observado antes do pulso;
- se pelo menos um bit realmente mudou.

## 14. Experimentos

### Rodada 1 - checksum

- Modo 0: 1000 quadros.
- Modo 1: 1000 quadros.
- Modo 2: 1000 quadros.
- Modo 3: 1000 quadros.

### Rodada 2 - CRC

- Modo 1: 1000 quadros.
- Modo 2: 1000 quadros.
- Modo 3: 1000 quadros.

Total obrigatorio: **7000 quadros**. Entre as duas rodadas, somente o algoritmo do campo `DETECCAO` muda.

## 15. Registros e medicoes

O RX enviara linhas CSV pela USB para o computador. Colunas definidas:

```text
tecnica,modo,slot,seq_esperada,seq_rx,len,p0_rx,p1_rx,det_rx,
residuo,decisao,p0_real,p1_real,payload_confere,status,
estado_seq,lacuna_seq,tempo_verificacao_us,tempo_recuperacao_us,
timestamp_us
```

O nome do arquivo e a pasta da coleta identificam a rodada. `estado_seq` informa
se o numero foi o primeiro, chegou em ordem, abriu uma lacuna ou era repetido/
antigo; `lacuna_seq` informa quantos numeros foram pulados quando houver lacuna.

O TX medira apenas o tempo de calculo do checksum ou CRC, sem incluir a transmissao UART. O RX medira apenas o tempo de verificacao do algoritmo, sem incluir recepcao nem impressao do CSV.

Ao fim de cada grupo de 1000 quadros, devem ser apresentados:

- quantidade de `OK`;
- quantidade de `CORROMPIDO`;
- quantidade de `PERDIDO`;
- quantidade de `NAO_DETECTADO`;
- taxa de nao deteccao;
- tempo minimo, medio e maximo de calculo no TX;
- tempo minimo, medio e maximo de verificacao no RX;
- overhead de 4 bytes por quadro;
- tempo de recuperacao apos perda.

## 16. Constantes compartilhadas pelos tres codigos

```text
BAUD_UART       = 115200
PIN_TX2         = 17
PIN_RX2         = 16
PIN_RUIDO       = 25
MARCADOR_INICIO = 0x7E
MARCADOR_DEBUG  = 0xD5
TAMANHO_PAYLOAD = 2
TAMANHO_QUADRO  = 6
PERIODO_QUADRO  = 20000 us
PAUSA_MARCADOR  = 2000 us
TIMEOUT_QUADRO  = 5000 us
PAUSA_DEBUG     = 4000 us
```

Essas definicoes devem aparecer de forma identica em `tx.ino`, `rx.ino` e `noise.ino` para que as tres placas interpretem o mesmo protocolo.
