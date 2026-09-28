# PrecisionPitchShift — Mudança de Tom de Alta Precisão (VST3 p/ Audacity)

Efeito VST3 v1.0.2 que transpõe **qualquer referência de origem X para qualquer
referência de destino Y** (`pitch_factor = Y/X` aplicado globalmente ao
espectro), preservando duração, taxa de amostragem do host, imagem estéreo e
a maior banda representável (`Nyquist = Fs/2` — **sem** corte fixo em 20 kHz,
**sem** resampling interno, **sem** compressor/limiter/EQ).

> Pitch shifting perfeito não existe. Este projeto promete apenas o que mede:
> máxima fidelidade possível com o mínimo de artefatos possível, tudo
> demonstrado na suíte de testes (`tests/`, `TEST_REPORT.md`).

---

## 1. Arquitetura

```text
VST3 Plugin (src/plugin: processor/controller/parameters/factory)
    ↓  (blocos do host, float32/64 → double interno)
DSP Engine (src/dsp: PitchEngine — streaming, multicanal acoplado)
    ↓
Pitch Shift Engine (STFT 75% overlap → spectral mapping → iSTFT overlap-add)
    ↓
Analysis / Reconstruction
  phase_processor.*    propagação de fase do vocoder + peak-rate locking
  spectral_processor.* janela, frequência instantânea, crest de transiente
  alias_protection.*   guarda de Nyquist dependente do fator (sem 20 kHz fixo)
  gain_control.*       proteção de ganho global com latch (não é compressor)
  fft.* / analysis.*   FFT radix-2 float64 + helpers de medição
GUI (src/gui/editor.*: nula por padrão → UI genérica do host; hook VSTGUI opcional)
```

O núcleo DSP (`src/dsp`) é **100% independente do VST3**: compila sem o SDK,
é exercitado pelos testes C++ (`tests/cpp`) e pelo CLI de render offline
(`tools/cpp/pps_render`, mesmo `PitchEngine` do plugin). Ferramentas de
diagnóstico em `tools/python/analyze.py`.

## 2. Algoritmo escolhido e justificativa (decidido por medição)

**Vocoder de fase com mapeamento espectral de Dirichlet + peak-rate locking
(“Engine B”, padrão ligado).** Parâmetros por taxa do host:

| Fs do host | FFT (High Precision) | Hop | Latência (= N) |
|---|---|---|---|
| 44.1 / 48 kHz | 4096 | 1024 | 85 / 85 ms |
| 88.2 / 96 kHz | 8192 | 2048 | 93 / 85 ms |
| 176.4 / 192 kHz | 16384 | 4096 | 93 / 85 ms |

Modo **Efficient** usa metade da FFT (metade da latência/CPU).

Alternativas avaliadas e **rejeitadas por medição** (ver `TEST_REPORT.md`):

* **Engine A** (propagação livre por bin, sem lock): espúrios de **−19 dB**
  a ~15 Hz das parciais em stacks estáticos + colapso de −14 dB em parciais
  com alinhamento desfavorável. Causa-raiz identificada e quantificada:
  o estimador de frequência instantânea só resolve ±frameRate/2 ao redor do
  centro do bin — bins na saia (>~2 bins do centro) sofrem **wrap de
  ±frameRate** (ex.: 393.1 em vez de 440 Hz a 48 kHz), e a interpolação
  mistura taxas wrapped/corretas.
* **Identity phase locking absoluto** (cópia da fase do pico): −8.7 dB e
  colapso de nível (−9 dB) — sidelobes Hann têm relação de fase exata com o
  pico; forçar igualdade destrói a soma. Rejeitado.
* **Janela Blackman-Harris** (−92 dB sidelobes): piorou (−11 dB) — lóbulo
  principal mais largo = mais bins em batimento. Rejeitado; Hann mantida.
* **Signalsmith Stretch / Rubber Band**: não adotados como motor — exigiriam
  etapa interna de resampling (conflito com o requisito
  `Fs_processing = Fs_host` lido estritamente) e/ou trazem comportamento de
  banda menos auditável e licenças mais restritivas. O vocoder próprio é
  totalmente auditável, usa `Fs` explícito e venceu nos testes A–F.

O que o Engine B faz (tudo medido antes/depois):

1. **Mapeamento espectral de Dirichlet** (Lanczos-8 com phase twist causal):
   interpolação bandlimited EXATA do espectro complexo, em vez de interpolação
   linear de magnitude/fase. Linear é matematicamente errado aqui: o espectro
   oscila (alternância π entre bins adjacentes) mais rápido que o espaçamento
   dos bins — interpolar através disso gerava valores sem sentido e colapsos
   de nível de até **−14 dB** conforme o alinhamento (seno de 660 Hz → 0.19×;
   varredura 200–2000 Hz: níveis 0.54–1.10×). Com Dirichlet: **0.87–1.00×**
   (pico verdadeiro 0.998–1.000×, independente do alinhamento) — crítico para
   guitarra/voz com vibrato, onde o alinhamento varre continuamente (sem isso,
   o vibrato vira tremolo áspero: "phaser").
2. **Peak-rate locking**: cada bin de saída avança na taxa do pico de
   **análise** mais próximo (picos = centros de parciais, onde a estimativa
   é inequívoca; picos exigem ≥6 dB de contraste — ripples de sidelobe não
   viram guias). Espúrios: **−19 dB → −64 dB**; balanço estéreo preservado.
3. **Ancoragem de ataque seletiva por bin**: só bins com energia NOVA
   (acima de −80 dB do pico do frame E crescidos >2.5× ante o frame anterior)
   re-ancoram fase; todo o resto propaga. Moldura global impulsiva (crest
   > 10, cliques isolados) ancora tudo. Por quê: re-ancorar TODOS os bins a
   cada bateria chutava a fase das parciais sustentadas 4×/segundo (coro /
   "phaser" audível em guitarra e voz em mixes densos — diagnosticado num
   caso real: 94% dos frames ancorando). Medido em mix denso: só 4.36% dos
   bins ancoram; ataques continuam nítidos (impulso: pico a +14 amostras).

`Fs_processing = Fs_host` sempre; limite = `Fs/2`; conteúdo que excederia
Nyquist após o shift é descartado com fade raised-cosine suave (nunca
dobrado para dentro da banda); tudo interno em `float64`.

## 3. Parâmetros (interface)

| Parâmetro | Faixa | Padrão |
|---|---|---|
| Frequencia de Origem X | 100–1000 Hz (0.01 Hz) | 440.00 Hz |
| Frequencia de Destino Y | 100–1000 Hz (0.01 Hz) | 444.00 Hz |
| Fator de Tom (somente leitura) | Y/X, 9 casas | 1.009090909 |
| Processamento | Alta Precisao / Eficiente | Alta Precisao |
| Protecao de Ganho Automatica | Ligado/Desligado | Ligado |
| Teto | −6.0 … −0.1 dBFS | −1.0 dBFS |
| Bypass | — | Off |

Nomes e rótulos em PT-BR (o Audacity exibe os textos do plugin como estão).
Valores numéricos são expostos sem unidade (o host anexa "Hz"/"dBFS"
sozinho). Entrada de texto digitada (quando o host oferece) aceita vírgula
decimal. Caixas de texto + sliders lado a lado, como no Compressor nativo,
só seriam possíveis com um editor customizado (VSTGUI) — a UI genérica do
Audacity para VST3 desenha apenas sliders/caixinhas; ver §7.7.

A taxa do host aparece apenas como informação (o motor sempre usa `Fs_host`).
Sem autotune, sem detecção de tom, sem correção de formantes: transformação
global `f_out = f_in × (Y/X)`.

## 4. Compilação

Pré-requisitos: CMake ≥ 3.22 e o **VST3 SDK** (não incluído; licença
Steinberg — ver `LICENSES/`). Duas formas de obter o SDK:

```bat
:: (a) cópia local (recomendado, funciona offline), ex. tag v3.7.14_build_55:
cmake -B build -S . -DPPS_VST3_SDK_DIR="C:/SDKs/vst3sdk"
:: (b) download automático via FetchContent (requer rede + git):
cmake -B build -S . 
```

Compilar (alvo: **Windows x64, MSVC, Release, VST3**):

```bat
cmake -B build -S . -DPPS_VST3_SDK_DIR="C:/SDKs/vst3sdk"
cmake --build build --config Release
:: resultado: build/VST3/Release/PrecisionPitchShift.vst3/
```

Notas:

* Gerador single-config (Ninja/MinGW): use `-DCMAKE_BUILD_TYPE=Release`.
* Somente DSP/testes/CLI (sem SDK): `cmake -B build -S . -DPPS_BUILD_VST3=OFF`
  seguido de `ctest` dentro de `build` (valida o núcleo sem Steinberg).
* Editor customizado VSTGUI: `-DPPS_WITH_VSTGUI=ON` (padrão OFF — o Audacity
  usa a UI genérica de parâmetros, totalmente funcional).
* Validado aqui: build+`ctest` do núcleo (MinGW) e compilação dos 4 objetos
  do plugin contra os headers reais do SDK v3.7.14 (link final do bundle
  requer MSVC — o SDK 3.7.14 não compila integralmente no MinGW por
  pendências próprias dele, ex. `std::aligned_alloc` em
  `dataexchange.cpp`; sem relação com este código).

## 5. Instalação no Audacity (Windows)

1. Copie a pasta `PrecisionPitchShift.vst3/` para um destes locais:
   * `C:\Program Files\Common Files\VST3\` (recomendado, todos os usuários), ou
   * `%APPDATA%\VST3\` (somente seu usuário — crie a pasta se não existir).
2. No Audacity: `Efeitos → Gerenciador de plug-ins (Plug-in Manager)` →
   `Verificar (Rescan)` → ative **PrecisionPitchShift** → `Ativar`.
3. Uso: selecione o áudio → `Efeitos → PrecisionPitchShift` → informe
   Origem X e Destino Y → `Aplicar`. O Audacity processa na taxa do projeto;
   **o plugin nunca altera a taxa** — para material em 96/192 kHz, configure a
   taxa do projeto do Audacity de acordo antes de importar/renderizar
   (o formato final exportado é controlado pelo Audacity, não pelo plugin).
4. Latência (`= FFT size`, ex. 4096 @48 kHz) é reportada ao host via
   `getLatencySamples()` e compensada automaticamente.

CLI offline (mesmo motor, para testes e batch):

```bat
pps_render --in in.wav --out out.wav --source 440 --target 444 [--quality high|efficient] [--autogain on|off] [--ceiling-db -1.0] [--depth 16|24|32]
pps_render --in in.wav --dump-bins 30,48 --dump-frames 8 --dump-out bins.csv   :: diagnóstico
```

## 6. Testes e resultados (resumo — detalhes em `TEST_REPORT.md`)

* `tests/cpp/dsp_tests.cpp` — **30/30**: matemática do fator, FFT roundtrip
  (1.3e-13), guarda anti-alias, política de FFT, senos X→Y (erro ≤ 0.023 Hz),
  harmônicos (<0.06 cents por parcial), passthrough (±0.2 dB), impulso
  (smear ~±500 amostras, pico −13), estéreo bit-idêntico, duração exata,
  30 kHz preservado a 192 kHz, sem foldback (−125 dB), roundtrip (0.08 cents),
  teto de ganho, Engine A/B.
* `tests/python/run_suite.py` — **36/36** em 44.1/48/88.2/96/176.4/192 kHz:
  7 pares X→Y, sweeps até Nyquist, tons ultrassônicos 20/30/40/60 kHz exatos,
  preservação acima de 20 kHz, estéreo, roundtrips a 48 e 192 kHz.
* `tools/python/analyze.py` — comparador Original×Processado (espectro até
  Nyquist, espectrogramas, tabela de bandas): ver PNGs/MDs em
  `tests/python/results/`.

## 7. Limitações conhecidas (honestas)

1. **Pitch shifting perfeito não existe.** Resíduo medido: espúrios até
   ~−43 dB em tons estáticos puros ( Ripple de frame-rate ±46.9 Hz a 48 kHz);
   em material musical (parciais móveis + mascaramento) fica abaixo da
   audibilidade prática. Nunca afirmamos “zero artefatos”.
2. **Conteúdo que ultrapassaria Nyquist é perdido** (física, não escolha):
   em shift para cima, a banda de entrada acima de `(Fs/2)/fator` não tem
   destino representável; o fade suave evita aliasing, mas a informação some.
   Documentado em `alias_protection.*`.
3. **Transientes**: preservados a ~±500 amostras (detecção dupla + âncora);
   pré-eco residual de ~0.3 ms existe em cliques isolados (típico de vocoders;
   modo Efficient reduz pela metade com FFT menor).
4. **Ganho automático é causal** (latch com rampa de 5 ms): em streaming, um
   pico tardio reduz o ganho dos trechos seguintes — em render offline isso
   equivale a ajuste global somente se o pico máximo vier cedo; para masters
   críticos, faça uma passada de medição ou ajuste manual. Nunca comprime:
   ganho escalar único, sem ratio/knee.
5. **Estéreo**: canais processados com mapeamento idêntico e decisões
   compartilhadas (diferença L−R = 0.0 em entrada diótica); coerência de fase
   intercanal é de primeira ordem, não há correção formântica nem
   alargamento artificial.
6. **VST3 não governa a exportação**: taxa/profundidade do arquivo final são
   do Audacity; o plugin garante apenas não fazer resampling interno.
7. **GUI customizada VSTGUI** incluída como hook desativado por padrão
   (não verificada contra build VSTGUI aqui); a UI genérica do Audacity
   expõe 100% dos parâmetros.

## 8. Problemas restantes / trabalho futuro

* Validação de escuta formal (MUSHRA) em material real variado — as métricas
  objetivas estão completas; escuta crítica documentada é o próximo passo.
* Build final do bundle `.vst3` + teste de carga no Audacity em máquina com
  MSVC (aqui: validado até objetos + configure; link requer MSVC).
* Confronto medido contra o **SBSMS** (modo "alta qualidade" do Audacity):
  o comparativo atual (`COMPARACAO.md`) cobre o SoundTouch (modo padrão);
  falta o rival mais forte.
* Interpolação cúbica de magnitude (ganho potencial de ~1 dB em picos de
  lóbulo com `frac` desfavorável; risco baixo, não priorizado).
* Teste com vibrato/chorus denso para calibrar falsos positivos do detector
  de transiente em material extremo.
