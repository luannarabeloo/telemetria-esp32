# Plano das evidências exigidas

## 1. Caso real de erro não detectado

Não é permitido inventar um exemplo. Primeiro rode os experimentos e execute `gerar_tabelas.ps1`. Abra `resultados/casos_nao_detectados.csv` e escolha uma linha cujo `status` seja `NAO_DETECTADO`.

Para esse mesmo número de quadro, procure a linha no CSV do ESP32 de ruído. Registre:

1. os bytes protegidos enviados: `SEQ`, `LEN`, `P0`, `P1` e detecção;
2. os bytes que o RX recebeu;
3. as posições `B?.b?` que o ruído tentou inverter e o `mudou_mask`;
4. a conta manual do checksum ou CRC usando os bytes recebidos;
5. a conclusão: o payload mudou, mas o resíduo ainda teve o valor aceito, por isso houve `NAO_DETECTADO`.

No checksum, refaça a soma em complemento de 1: some os bytes; sempre que passar de `0xFF`, some o excesso de volta; incluindo o checksum recebido, o resultado aceito é `0xFF`. No CRC-8, faça a divisão polinomial módulo 2 com gerador `0x07`; incluindo o byte de CRC recebido, o resto aceito é zero.

## 2. Timeout justificado

O RX candidato registra `timestamp_us` em todas as linhas e `tempo_recuperacao_us` no primeiro quadro concluído depois de uma perda. Isso fornece a evidência por `micros()` solicitada pelo enunciado.

Para demonstrar:

1. escolha uma rodada que contenha `PERDIDO`;
2. mostre a linha perdida e a linha seguinte;
3. compare os timestamps;
4. explique que o período normal é 20.000 µs, o timeout interno é 5.000 µs depois de `0x7E` e a tolerância de ausência do próximo início é 10.000 µs;
5. mostre `tempo_recuperacao_us` na primeira linha concluída após a perda.

O valor não precisa ser exatamente igual ao parâmetro, pois inclui espera, processamento e escalonamento dos ESP32. Ele precisa ser compatível com a ordem de grandeza definida.

## 3. Teste de predição dos próximos dez quadros

Antes da demonstração, use as tabelas medidas. Para o modo secreto escolhido pelo professor, use as proporções da mesma técnica e do mesmo modo.

Exemplo: se uma rodada de 1000 quadros teve 820 OK, 170 corrompidos, 8 perdidos e 2 não detectados, a previsão aproximada para dez é 8 OK, 2 corrompidos e provavelmente 0 perdido/0 não detectado. Explique que é previsão estatística: o resultado exato pode variar porque as posições do ruído são aleatórias e o pulso físico nem sempre altera o nível amostrado.

Não prometa os dez estados individuais com certeza. Registre a previsão antes de iniciar e depois compare com as dez linhas reais do RX.

## 4. “Quebrar o próprio protocolo”

Use um caso real de `NAO_DETECTADO` encontrado nos dados. Ele já é um padrão de ruído que a técnica não capturou.

- Para checksum, duas alterações podem compensar a soma: um byte aumenta e outro diminui pelo mesmo valor. A soma final pode continuar válida apesar de o conteúdo ter mudado.
- Para CRC-8, existe um conjunto específico de alterações cujo polinômio de erro é divisível pelo gerador. Nesse caso, o resto continua zero. É menos fácil de produzir por acaso, então podem ser necessárias mais rodadas.

Apresente somente o padrão que realmente apareceu e associe cada posição de bit ao log do ruído.

## 5. Tabelas e consistência

Para cada linha da tabela confirme:

`OK + CORROMPIDO + PERDIDO + NAO_DETECTADO = 1000`

E calcule:

`taxa de não detecção = 100 × NAO_DETECTADO / (CORROMPIDO + NAO_DETECTADO)`

Não inclua `PERDIDO` no denominador porque, nesse caso, o RX não chegou a aplicar uma verificação completa ao quadro.

## 6. Conteúdo do GitHub ao final

O repositório deve conter:

- `tx/tx.ino`, `rx/rx.ino` e `noise/noise.ino` compiláveis e comentados;
- especificação escrita do protocolo;
- CSVs do RX e do ruído, identificados por técnica e modo;
- tabelas de checksum e CRC;
- evidências do timeout e do caso não detectado;
- um README com montagem, portas, ordem de teste e troca de técnica.

Não publique chaves, senhas nem arquivos temporários da IDE. O envio ao GitHub deve ser feito somente depois que o RX candidato passar pelo teste de modo 0 e pelas rodadas com ruído.

