# Análise das evidências experimentais

Este documento reúne os resultados já obtidos. Ele não substitui o relatório final, mas fornece as tabelas, contas e justificativas que podem ser usadas nele.

## 1. Rodadas realizadas

| Técnica | Modo | OK | Corrompido | Perdido | Não detectado | Total | Taxa de não detecção |
|---|---:|---:|---:|---:|---:|---:|---:|
| Checksum | 0 | 1000 | 0 | 0 | 0 | 1000 | 0% |
| Checksum | 1 | 816 | 180 | 4 | 0 | 1000 | 0% |
| Checksum | 2 | 651 | 343 | 5 | 1 | 1000 | 0,2907% |
| Checksum | 3 | 404 | 589 | 7 | 0 | 1000 | 0% |
| CRC-8 | 1 | 807 | 189 | 4 | 0 | 1000 | 0% |
| CRC-8 | 2 | 655 | 342 | 3 | 0 | 1000 | 0% |
| CRC-8 | 3 | 399 | 584 | 16 | 1 | 1000 | 0,1709% |

A taxa foi calculada como:

`100 × NAO_DETECTADO / (CORROMPIDO + NAO_DETECTADO)`

Perdidos não entram no denominador porque o RX não concluiu a aplicação do algoritmo nesses quadros.

## 2. Erro não detectado real — checksum, modo 2, slot 569

### Evidência registrada

- Quadro protegido enviado: `38 02 FF 38 8D`.
- Quadro protegido recebido: `38 02 38 8D FF`.
- Payload verdadeiro: `FF 38`.
- Payload recebido: `38 8D`.
- Log do ruído: `B0.b5|B3.b6`, com `mudou_mask = 0x03`.
- Decisão tomada somente com o quadro principal: `ACEITO`.
- Comparação posterior com a verdade: payload diferente.
- Classificação final: `NAO_DETECTADO`.

Os bytes têm a ordem `SEQ LEN P0 P1 CHECKSUM`. Os valores verdadeiros permitem reconstruir o quadro enviado. O log do ruído informa os pulsos físicos programados; a diferença byte a byte entre enviado e recebido mostra a corrupção efetivamente observada pelo RX.

### Cálculo do checksum no transmissor

Bytes protegidos antes do checksum: `38 02 FF 38`.

1. `38 + 02 = 3A`.
2. `3A + FF = 139`; volta-se o carry: `39 + 01 = 3A`.
3. `3A + 38 = 72`.
4. Complemento de 1 de `72`: `8D`.

Por isso o quadro correto termina em `8D`.

### Cálculo no receptor sobre os bytes corrompidos

O RX recebeu `38 02 38 8D FF` e somou inclusive o checksum recebido:

1. `38 + 02 = 3A`.
2. `3A + 38 = 72`.
3. `72 + 8D = FF`.
4. `FF + FF = 1FE`; volta-se o carry: `FE + 01 = FF`.

O resíduo ficou `FF`, exatamente o valor aceito pelo algoritmo. Portanto o RX aceitou o quadro. Somente depois, ao comparar `38 8D` com a cópia verdadeira `FF 38`, descobriu que o conteúdo estava errado. Esse é um erro não detectado real.

### Bits diferentes

O XOR mostra quais bits diferem entre os bytes enviados e recebidos:

- `P0: FF XOR 38 = C7`, bits `b7, b6, b2, b1 e b0`.
- `P1: 38 XOR 8D = B5`, bits `b7, b5, b4, b2 e b0`.
- `CHECKSUM: 8D XOR FF = 72`, bits `b6, b5, b4 e b1`.

O pulso físico pode afetar a amostragem e o enquadramento da UART; por isso as posições programadas pelo injetor e todas as diferenças reconstruídas pelo RX não precisam ter correspondência um-para-um.

## 3. Segundo erro não detectado — CRC-8, modo 3, slot 53

- Enviado: `34 02 FF 34 7C`.
- Recebido: `34 02 0F 04 F8`.
- CRC original calculado pelo TX: `7C`.
- Resíduo calculado pelo RX sobre o quadro recebido: `00`.
- Decisão: `ACEITO`.
- Payload verdadeiro: `FF 34`.
- Payload recebido: `0F 04`.
- Resultado: `NAO_DETECTADO`.

O polinômio de erro produzido por essa combinação de alterações é divisível pelo gerador usado pelo CRC-8. Assim, embora vários bits tenham mudado, o resto continuou zero. Este caso pode ser apresentado como evidência adicional; para a conta manual principal, o caso de checksum é mais simples.

## 4. Timeout e recuperação

Um exemplo claro está no checksum, modo 1:

| Slot | Sequência esperada | Sequência recebida | Status | Estado da sequência | Recuperação |
|---:|---:|---:|---|---|---:|
| 443 | BA | BA | OK | EM_ORDEM | 0 µs |
| 444 | BB | BB | PERDIDO | NA | 0 µs |
| 445 | BB | BC | OK | LACUNA de 1 | 19.993 µs |
| 446 | BD | BD | OK | EM_ORDEM | 0 µs |

O slot 444 não pôde ser concluído. O RX o declarou perdido e não esperou retransmissão, pois o enlace é somente TX → RX. No quadro seguinte recebeu `BC`; como ainda esperava `BB`, registrou lacuna de um quadro. A recuperação demorou 19.993 µs, praticamente o período configurado de 20.000 µs entre inícios.

O arquivo `evidencias_timeout_recuperacao.csv` contém todos os eventos semelhantes. Ele constitui um log de `micros()` e pode ser usado como a evidência temporal solicitada.

## 5. Overhead

O quadro principal tem seis bytes:

`7E | SEQ | LEN | P0 | P1 | DETECCAO`

- Payload útil: `P0` e `P1` = 2 bytes.
- Controle do protocolo: marcador, sequência, comprimento e detecção = 4 bytes.
- Tamanho do quadro principal = 6 bytes.
- Overhead em relação ao payload = `4 / 2 × 100 = 200%`.
- Eficiência útil dentro do quadro = `2 / 6 × 100 = 33,33%`.

A cópia experimental `D5 | P0 verdadeiro | P1 verdadeiro` acrescenta três bytes na bancada, mas não faz parte do quadro protegido normal. Se ela for incluída no tráfego experimental total, são nove bytes transmitidos por amostra e apenas dois são telemetria útil.

## 6. Predição para dez quadros

O arquivo `predicao_10_quadros.csv` contém as probabilidades medidas e uma previsão inteira que soma dez. Exemplos:

- Checksum, modo 1: aproximadamente 8 OK e 2 corrompidos.
- Checksum, modo 2: aproximadamente 7 OK e 3 corrompidos.
- Checksum, modo 3: aproximadamente 4 OK e 6 corrompidos.
- CRC, modo 1: aproximadamente 8 OK e 2 corrompidos.
- CRC, modo 2: aproximadamente 7 OK e 3 corrompidos.
- CRC, modo 3: aproximadamente 4 OK e 6 corrompidos.

Perdas e não detecções tiveram frequência inferior a uma ocorrência esperada a cada dez quadros, por isso aparecem como zero na previsão inteira. Ainda podem ocorrer no teste real. A previsão é estatística, não uma promessa dos estados exatos.

## 7. Como “quebrar” o protocolo

O slot 569 já quebrou o checksum: vários bytes mudaram de forma compensatória e a soma com carry circular permaneceu `FF`. O algoritmo verificou a soma, não o significado do payload, e aceitou o quadro errado.

O slot 53 fez o mesmo com o CRC: o padrão total de erro produziu resto zero para o polinômio `0x07`. CRC é mais forte para muitos padrões, mas nenhum CRC curto detecta todos os padrões possíveis.

## 8. Tempos de cálculo no TX

Os tempos abaixo medem somente a execução do algoritmo que produz o byte de detecção. Eles não incluem a espera entre quadros nem a transmissão UART.

| Técnica | Quadros | Mínimo | Médio | Máximo |
|---|---:|---:|---:|---:|
| Checksum de 1 complemento | 1000 | 0,421 µs | 0,424 µs | 3,946 µs |
| CRC-8, polinômio 0x07 | 1000 | 1,358 µs | 1,388 µs | 1,425 µs |

Nesta implementação, o CRC demorou aproximadamente `1,388 / 0,424 = 3,27` vezes o tempo médio do checksum. Isso é coerente com o código: o checksum faz somas e tratamento do carry, enquanto o CRC percorre oito bits para cada byte protegido. O pico de 3,946 µs do checksum é uma medição isolada e pode resultar de interrupções ou escalonamento do ESP32; a média é mais representativa do custo habitual.

Os logs originais e a consolidação estão preservados na pasta `resultados`.

