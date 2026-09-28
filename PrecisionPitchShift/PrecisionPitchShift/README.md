## Correção de parâmetros na UI genérica do Audacity

Os parâmetros editáveis são exportados com `kCanAutomate` para que a UI genérica do Audacity os apresente. Isso não obriga o usuário a criar automação. Os `RangeParameter` usam agora seus valores físicos reais (Hz/dBFS) no descritor VST3, em vez de expor incorretamente a faixa normalizada [0,1].

Após substituir uma instalação anterior do plugin, reinicie o Audacity e faça uma nova verificação de plugins, se necessário.

# PrecisionPitchShift

VST3/DSP de mudança global de tom/pitch por razão de frequência:

`factor = targetFrequency / sourceFrequency`

O plugin não tenta detectar a tonalidade da música. O usuário informa a frequência de referência de origem `X` e a frequência de referência de destino `Y`; a mesma razão é aplicada ao conteúdo espectral.

## Objetivos técnicos

- manter a duração da gravação;
- não fazer resampling interno como etapa do pitch shift;
- trabalhar na taxa de amostragem fornecida pelo host;
- não impor um low-pass fixo de 20 kHz;
- preservar o máximo possível da banda representável pela taxa de amostragem;
- controlar aliasing quando uma componente deslocada ultrapassa Nyquist;
- minimizar artefatos de fase, transientes e imagem estéreo;
- manter o processamento interno em `double` quando possível;
- deixar proteção de ganho automática desligada por padrão.

A taxa de amostragem final do arquivo continua sendo responsabilidade do host/Audacity. O requisito do plugin é não inserir resampling próprio na cadeia DSP.

## Estado da engine

A engine usa STFT com janela Hann e 75% de overlap. O mapeamento espectral é uma interpolação complexa windowed-sinc/Lanczos de suporte finito (8 taps por lado) com correção de fase causal. A fase usa estimativa de frequência instantânea, peak-rate locking opcional e ancoragem seletiva de ataques.

Nesta revisão, o phase-rate path foi melhorado de duas formas:

1. os picos de análise usados como guias possuem rastreamento temporal com hysteresis, evitando troca de guia por pequenas mudanças geométricas de bin;
2. para shifts diferentes de unity, a taxa de fase é integrada com passo trapezoidal entre frames, reduzindo a modulação em degraus na taxa do STFT quando a frequência da parcial se move, como em vibrato.

O segundo ponto é importante: a taxa de frame em 192 kHz com FFT 16384 e hop 4096 é 46,875 Hz. Resíduos síncronos nessa frequência ainda podem existir, pois fazem parte das limitações do phase-vocoder; o objetivo é evitar que o mecanismo de locking os amplifique desnecessariamente.

## Realtime safety

O caminho de áudio foi estruturado para não alocar dinamicamente memória dentro do callback realtime normal:

- FIFO de entrada/saída com ring buffer de capacidade fixa;
- scratch buffers pré-alocados na configuração;
- lista de picos pré-alocada por frame;
- `FFT` reutiliza seu workspace;
- nenhum `vector::erase(begin())` no caminho de áudio;
- o plugin não redimensiona seus buffers no callback. Caso um host viole `maxSamplesPerBlock`, o plugin falha de forma transparente para bypass em vez de alocar memória.

O script `tools/python/check_realtime_safety.py` faz uma checagem estática simples para chamadas comuns de alocação dinâmica dentro das funções realtime.

## Parâmetros

- **Frequência de Origem X:** 100–1000 Hz, 0,01 Hz.
- **Frequência de Destino Y:** 100–1000 Hz, 0,01 Hz.
- **Processamento:** Alta Precisão / Eficiente.
- **Proteção de Ganho Automática:** desligada por padrão.
- **Teto:** -6,0 a -0,1 dBFS.
- **Bypass:** disponível.

O fator é mostrado somente como informação derivada de `Y/X`.

Os parâmetros são deliberadamente não automatizáveis nesta versão: mudanças de fator em meio ao processamento exigem tratamento sample-accurate mais sofisticado que a interface atual não implementa. O bypass também não é marcado como automatizável por essa mesma razão conservadora.

## Proteção de ganho

A engine não comprime nem limita. A proteção de ganho, quando habilitada, aplica somente um ganho global escalar.

No `pps_render --autogain on`, são feitas duas passagens: a primeira mede o maior pico global; a segunda renderiza com ganho fixo. Isso evita o pumping do modo causal de tempo real e preserva a dinâmica relativa.

## Nyquist e banda ultrassônica

O processamento não contém um corte fixo em 20 kHz. O limite físico é `Fs/2`.

Quando um pitch shift ascendente moveria uma componente além de Nyquist, ela não pode ser reproduzida sem aliasing na mesma taxa de amostragem. O `alias_protection` aplica uma transição dependente do fator para evitar foldback; a versão atual usa uma guarda estreita de 1% da grade espectral, em vez do guard de 5% usado anteriormente, para sacrificar menos banda desnecessariamente.

Isso não significa que o conteúdo próximo de Nyquist possa ser preservado em situações matematicamente impossíveis; nesses casos a limitação é do próprio sistema amostrado.

## Build DSP/CLI/testes sem SDK VST3

```bash
cmake -B build -S . -DPPS_BUILD_VST3=OFF -DPPS_BUILD_TESTS=ON -DPPS_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Para validar safety:

```bash
python3 tools/python/check_realtime_safety.py
```

## Build VST3

Defina `PPS_VST3_SDK_DIR` para uma cópia local do SDK VST3 e use:

```bash
cmake -B build-vst3 -S . -DPPS_VST3_SDK_DIR="C:/SDKs/vst3sdk" -DPPS_BUILD_TESTS=ON -DPPS_BUILD_CLI=ON
cmake --build build-vst3 --config Release
```

O alvo do release é Windows x64/MSVC. O bundle resultante deve ser `PrecisionPitchShift.vst3`.

## Validação musical

Os testes sintéticos cobrem pitch, harmônicos, sweeps, impulsos, múltiplas taxas, estéreo, round-trip e regressão de sidebands de frame-rate. Eles não substituem escuta cega nem material musical real.

Para o problema conhecido de "phaser/eco" em guitarra/voz com vibrato, a validação deve usar um mix real a 192 kHz e medir explicitamente componentes em `±46,875 Hz` em torno das parciais. O gate da suíte sintética não é uma garantia de ausência de artefatos em mixes complexos.

## Limitações conhecidas

- pitch shifting perfeito não existe para todos os sinais;
- conteúdo que ultrapassaria Nyquist precisa ser limitado ou removido de forma anti-aliasing;
- o phase-vocoder ainda pode apresentar resíduos periódicos de frame-rate;
- a qualidade varia com material, fator e resolução espectral;
- o VST3 não controla a taxa de amostragem de exportação do Audacity;
- este pacote pode conter apenas o código-fonte do plugin se o SDK/toolchain necessário para linkar o bundle VST3 não estiver disponível no ambiente de build.

## Estrutura

```text
src/dsp/      engine independente do host
src/plugin/   wrapper VST3
src/gui/      editor/generic UI support
tests/cpp/    testes DSP
tools/cpp/    renderizador offline WAV
tools/python/ análise e safety checks
```
