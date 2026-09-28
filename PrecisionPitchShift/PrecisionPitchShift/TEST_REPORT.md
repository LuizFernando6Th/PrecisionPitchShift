# TEST_REPORT — revisão de engenharia local

Este relatório descreve somente resultados que foram reexecutados no pacote desta revisão. Resultados históricos de versões anteriores não são reapresentados como se fossem medições atuais.

## Escopo desta revisão

Foram aplicadas correções em:

- rastreamento temporal/hysteresis do pico-guia do phase locking;
- integração trapezoidal da taxa de fase entre frames para shifts não-unity;
- ring buffers de capacidade fixa no caminho DSP;
- scratch buffers e lista de picos pré-alocados;
- remoção de `vector::erase(begin())` e outras alocações evitáveis no callback DSP;
- limites X/Y coerentes com `factor` de 0,1 a 10;
- mudança de fator sem reset artificial da fase acumulada;
- proteção de ganho desligada por padrão;
- `pps_render --autogain on` convertido em render de duas passagens com ganho global fixo;
- parâmetros não marcados como automatizáveis sem implementação sample-accurate;
- validação de falhas de leitura/escrita do estado VST3;
- identificação do produto/URL do plugin;
- guard anti-aliasing reduzido de 5% para 1% da grade espectral;
- documentação e checker simples de realtime safety.

## Resultado atual

### Testes C++

Os 34/34 testes do alvo `pps_dsp_tests` passaram na build final da revisão.

Incluem:

- senos em múltiplos fatores X→Y;
- harmônicos;
- sweep/tons acima de 20 kHz;
- transientes;
- múltiplas taxas de amostragem;
- estéreo;
- round-trip;
- regressão de sideband na taxa de frame.

### Sanitizers

A build instrumentada com AddressSanitizer e UndefinedBehaviorSanitizer também passou os 34/34 testes DSP na revisão desta branch.

### Realtime safety

`tools/python/check_realtime_safety.py` passou. O check é estático e não substitui teste de alocação/runtime sob host real.

### Regressão frame-rate

O teste sintético usa:

- `Fs = 192000 Hz`
- `FFT = 16384`
- `hop = 4096`
- fator `444/440`
- componente vibratória de 440 Hz, `5,5 Hz`, `±25 cents`
- componente próxima a 454 Hz

A taxa de frame é exatamente:

`192000 / 4096 = 46,875 Hz`.

Na versão anterior desta revisão, a métrica combinada do teste estava em aproximadamente `-26,0 dBc`. Após rastreamento temporal do guia + integração trapezoidal da taxa de fase, ficou em aproximadamente `-29,5 dBc` sob a mesma métrica, uma melhora de aproximadamente `3,5 dB`.

Isso é uma regressão sintética; não é uma garantia de eliminação do efeito em um mix rock real. O guia-switch count permanece apenas diagnóstico, pois uma métrica global `switches / assignments` é contaminada pelas fronteiras móveis de influência dos picos e não representa qualidade perceptual de forma confiável.

## Limitações de validação

O pacote original fornecido para esta revisão não continha a suíte Python histórica referenciada em versões anteriores do `TEST_REPORT.md`. Portanto, os números históricos de `C++ 30/30 + Python 36/36` não foram tratados como reproduzidos.

Também não foi feito um confronto auditivo formal MUSHRA com várias pessoas.

O bundle VST3 final não foi linkado neste ambiente porque o SDK/toolchain alvo de Windows/MSVC não está disponível aqui. O código-fonte do wrapper VST3 permanece no pacote e o CMake documenta o build com SDK local.

## Próxima validação necessária

Para fechar a questão do "phaser/eco", renderizar o mesmo mix rock real a 192 kHz por dois caminhos usando exatamente o mesmo `PitchEngine`:

1. `pps_render` offline;
2. Audacity → VST3 → WAV.

Medir em ambos as parciais relevantes e os componentes em `±46,875 Hz`, mantendo Auto Gain desligado.

Gate recomendado para a correção atual: redução mínima de 12 dB nos sidebands atribuíveis à modulação frame-rate em relação à baseline anterior, sem aumento do erro de pitch acima de 0,05 Hz e sem perda do vibrato fundamental.
