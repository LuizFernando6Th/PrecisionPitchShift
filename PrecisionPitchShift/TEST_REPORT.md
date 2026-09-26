# TEST_REPORT — resultados medidos (nada aqui é inventado)

Ambiente da medição: Windows x64, Python 3.12 + numpy 2.5.2 / scipy 1.18.1,
núcleo DSP compilado com `g++ -O2` (MinGW/UCRT), mesmos fontes do plugin.
Logs integrais: `tests/python/results/cpp_tests.log` e `py_suite.log`;
artefatos visuais: `tests/python/results/*.png`, `*_bands.md`.

**Placar final: C++ 30/30, Python 36/36.**

## Teste A — senos (pitch absoluto)

| Par X→Y | Esperado | Medido @48 kHz | Erro |
|---|---|---|---|
| 440→444 | 444.000 | 444.010 | 0.010 Hz |
| 432→440 | 440.000 | 439.989 | 0.011 Hz |
| 440→442 | 442.000 | 442.003 | 0.003 Hz |
| 442→444 | 444.000 | 444.010 | 0.010 Hz |
| 444→432 | 432.000 | 431.991 | 0.009 Hz |
| 415→440 | 440.000 | 439.989 | 0.011 Hz |
| 440→415 | 415.000 | 414.990 | 0.010 Hz |

440→444 por taxa: 44.1k: 443.991 · 48k: 444.010 · 88.2k: 443.990 ·
96k: 444.012 · 176.4k: 443.990 · 192k: 443.978 (erro ≤ 0.023 Hz em todas).

## Teste B — harmônicos (mesma razão Y/X em todas as parciais)

Stack 440 Hz ×10 parciais → 444: pior parcial com **0.59 cents** de erro
(48 e 96 kHz). Espúrios (tol. 6 Hz): **−43.6 dB** @48 kHz e 96 kHz
(sensibilidade entre execuções ~±10 dB em stacks perfeitamente estáticos;
referência estável no teste C++ com tol. 25 Hz: −53.2 dB).

## Teste C — sweep até Nyquist + preservação > 20 kHz

* Duração preservada amostra-exata em 48/96/192 kHz.
* Tons ultrassônicos exatos: 20 k→20181.8, 30 k→30272.7, 40 k→40363.6 @96 k;
  20/30/60 k exatos @192 kHz (60 k→60545.5 Hz).
* Banda 20–30 kHz preservada (delta de energia +4.3 dB @96/192 k —
  positivo por concentração espectral do shift, sem corte).
* Top-octave sem foldback (≤ −159 dB de conteúdo anômalo).
* Tabela de bandas 0–96 kHz: `sweep192_bands.md`; espectrogramas
  `sweep192_gram_{ref,proc}.png` mostram energia até Nyquist sem degrau em
  20 kHz (linha ciano é só referência visual).

## Teste D — transientes

Impulso unitário 440→444 @48 k: resposta finita, pico 0.304, **drift −13
amostras** (0.27 ms) após compensação de latência; energia contida em
~±500 amostras. Sem NaN/inf, sem duplicação.

## Teste E — taxas + estéreo

* Todas as 6 taxas processadas na taxa nativa (`Fs_proc = Fs_host`).
* Entrada diótica → saídas L/R **bit-idênticas** (diff 0.00e+00).
* Balanço L/R de programa (−2.05 dB) preservado (−2.07 dB).

## Teste F — roundtrip 440→444→440

48 kHz: f0 final 440.020 Hz (**0.08 cents**). 192 kHz: 440.039 Hz
(**0.15 cents**). Dano cumulativo de duas passadas ≈ desprezível.

## Comparativo Engine A × B (decisão documentada)

| Métrica (stack 440→444 @48 k) | Engine A (livre) | Engine B (padrão atual) |
|---|---|---|
| Espúrios (tol. 25 Hz, seno) | −18.7 dB @428.5 Hz | **−53.2 dB** |
| Nível parcial 660 Hz (alinh. desfavorável) | 0.19× (−14 dB) | **0.95×** |
| Precisão de pitch | <0.05 cents | <0.06 cents (inalterada) |
| Balanço estéreo | −2.05→−0.03 dB (quebra) | −2.05→−2.07 dB (ok) |

Rejeitados também: phase-locking absoluto (−8.7 dB + colapso de nível) e
janela Blackman-Harris (−11 dB). Causas-raiz quantificadas no README §2.

## Modos de processamento @48 kHz (2 s mono)

High Precision (N=4096): 0.16 s · latência 4096 · spur −43.6 dB.
Efficient (N=2048): 0.11 s · latência 2048 · spur −48.2 dB.
(≈12×/18× mais rápido que tempo real no hardware de teste.)

## Validação do código VST3

* `processor.cpp`, `controller.cpp`, `factory.cpp`, `editor.cpp`: compilação
  limpa (`exit 0`, zero erros) contra os **headers reais do VST3 SDK
  v3.7.14** — 3 bugs reais foram encontrados e corrigidos por essa via
  (`setLatencySamples` inexistente → overrides `getLatencySamples/
  getTailSamples`; `DiscreteParameter` inexistente → `StringListParameter`;
  `UString128` usado como wrapper → `UString`).
* `cmake -B build_vst3 -DPPS_VST3_SDK_DIR=...`: **configure OK** com o SDK.
* Link final do bundle requer **MSVC** (o SDK 3.7.14 não linka integralmente
  no MinGW por pendências próprias: `std::aligned_alloc` ausente em
  `dataexchange.cpp`, falha de link do `validator`) — sem relação com este
  código; toolchain alvo documentada no README.
* Núcleo DSP/CLI/testes: build + `ctest` **30/30** via CMake.

## O que NÃO foi medido (declarado)

Escuta formal cega (MUSHRA) em material musical real variado; apenas
fixturas sintéticas + stack “musicish” estéreo. O comparador
`tools/python/analyze.py` está pronto para material do usuário.
