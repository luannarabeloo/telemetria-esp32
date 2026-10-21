# Checklist de conformidade

## Já implementado ou preparado

- [x] UART2 a 115200 bit/s e terra comum.
- [x] Quadro com marcador `0x7E`, sequência, comprimento, payload de 2 bytes e um campo de detecção.
- [x] Alternância entre checksum de 1 complemento e CRC-8 `0x07`.
- [x] Verdade posterior `0xD5` fora da decisão e da janela de ruído.
- [x] RX classifica OK, CORROMPIDO, PERDIDO e NAO_DETECTADO.
- [x] RX candidato registra sequência esperada, lacuna, repetição/atraso e volta 255→0.
- [x] RX candidato registra tempo de verificação, recuperação e timestamp.
- [x] Ruído aprovado possui modos 0 a 3, usa temporização física e registra posições/timestamp.
- [x] Scripts do computador salvam RX e ruído em CSV sem alterar o protocolo.
- [x] Script calcula tabelas e separa casos não detectados.

## Precisa ser confirmado no hardware

- [x] RX candidato compila.
- [x] Modo 0 resulta em 1000 OK e nenhuma perda falsa.
- [x] Checksum: modos 0, 1, 2 e 3 coletados.
- [x] CRC: modos 1, 2 e 3 coletados.
- [x] Totais de todas as rodadas somam 1000.
- [x] Pelo menos um caso real de não detecção foi preservado com o log de ruído correspondente.
- [x] Evidência de timeout/recuperação foi preservada.
- [ ] Teste de predição de dez quadros foi ensaiado.

## Precisa ser finalizado para entrega

- [x] Tabelas preenchidas com medições reais.
- [x] Cálculo manual do caso não detectado.
- [x] Captura ou tabela de `micros()` usada para justificar timeout.
- [x] Tempos mínimo, médio e máximo de cálculo do TX preservados para checksum e CRC.
- [ ] Relatório/apresentação final.
- [ ] Repositório GitHub organizado e conferido.
- [ ] Demonstração ao vivo ensaiada.
