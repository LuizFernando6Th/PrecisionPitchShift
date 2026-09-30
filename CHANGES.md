# CHANGES — modelo de fase com rotação compartilhada (v2)

Base: `c1f2cbf` (que já trazia o protótipo sem `getenv`, nunca compilado/testado).

## O que mudou

- **Motor (padrão do plugin):** fase de saída = fase de *análise* reinterpolada por
  canal + uma rotação `R[k]` **única para todos os canais**, avançada a
  `(f-1)·ω`. O integrador antigo (`f·ω`, por canal, sem realimentação) continua
  disponível **apenas** para comparação: `EngineConfig::sharedRotation = false`
  ou `pps_render --shared-rotation off`.
- **Sem variável de ambiente e sem chave de compilação.** Removida a opção CMake
  `PPS_LEGACY_PHASE` (deixava o plugin trocar de motor em silêncio). Verificado
  por `grep`, por `nm` em `libpps_dsp.a` e nos objetos/`.so` do plugin: nenhuma
  referência a `getenv`.
- **Twist:** continua igual ao motor original (`kFixTwistSign = false`), travado
  por teste.
- **`phaseLock` agora é respeitado** no caminho novo (antes era ignorado, e o teste
  "engineA" passava sem testar nada). Custo, só em modo de comparação
  (`--phaselock off`): ruído −2,65 dB (com lock: −0,77 dB). O plugin usa sempre
  `phaseLock = true`.
- Contadores de diagnóstico (`--count-transients`) voltam a funcionar.
- `check_realtime_safety.py` cobre também `mapSpectrum` e `updateSharedRotation`
  (verificação estática de tokens; não prova tempo real).
- **Não alterado:** parâmetros, latência reportada, formato de estado, UI,
  `src/plugin/` (0 arquivos), workflow.
- **Testes novos:** `tests/cpp/regression_tests.cpp` (`regression_tests` e
  `regression_tests_detect_legacy`). Sinais gerados em C++; nenhum arquivo de áudio.

## Números medidos (Linux, GCC 13, `-O2`, sandbox de 1 núcleo)

| gate (192 kHz, N=16384) | limite | rotação | legado |
|---|---|---|---|
| A bypass f=1,0, ruído mono | \|ΔRMS\|≤0,05 dB; nulo≤−120 dB (1–60 kHz) | +0,0000 dB; −217,5 dB | −4,07 dB; −0,7 dB |
| B ruído estéreo ρ=0,5, 440→444 | \|Δρ\|≤0,03; \|ΔRMS\|≤1 dB | 0,495→0,500; −0,45/−0,45 dB | 0,495→0,007; −4,64/−4,59 dB |
| C 118 tons estéreo (IPD/ILD) | ≤1°; ≤0,1 dB | 0,04°; 0,007 dB | até 43,7°; 0,26 dB |
| D comb de 30 tons × 3 fases | σ ≤ 0,5 dB | σ 0,002 dB | σ até 1,3 dB (sem. 1) / 2,5 dB |
| E rajada 1 kHz, 16 alinhamentos | mediana ≤3 e ≥6/16 em 0,9–1,1 | mediana 1,75; 8/16; pior 7,43 | mediana 10,25; 2/16; pior 17,54 |

Semente padrão (1). Sementes 2 e 3: produção passa 14/14 com margens parecidas; o
auto-teste do legado detecta os 5 gates nas 3 sementes (no gate D, semente 1, só 1
das 3 fases de tons falha: margem fina).

- **Gate E ≠ proposta original.** A razão de subida depende de onde a rajada cai na
  grade de hop: com a correção vai de 0,97 a 7,43 (8/16 alinhamentos dentro de
  0,9–1,1); no legado, de 1,04 a 17,5. O "274→269" do handoff vale para um
  alinhamento. Por isso o gate usa mediana e fração, não um único ponto.
- **Gates C e D foram desenhados para não serem vazios:** com tons a partir de n=0,
  ou numa grade densa, o legado passa. Precisa de onset após silêncio e de tons
  esparsos (conjunto de referência do `gen.py`); `--expect-legacy-fails` prova a
  detecção a cada `ctest`.
- Gates sintéticos pelo CLI: `G_v2` reproduz `results/syn_rot.txt` **linha a linha**;
  legado da v2 é **bit-idêntico** ao motor de `05b8d9d` nos 9 renders; v2 padrão é
  bit-idêntica ao protótipo `PPS_ROT=1` em 7/9 (os 2 diferentes são `--phaselock off`).
- `ctest`: 3/3 em 95 s. ASan+UBSan: sem erros (34/34, 14/14, 5/5).
- CPU, mesmo render estéreo de 6 s a 192 kHz: 19,2 s (novo) vs 38,8 s (original).
- Alvo VST3 compilado no Linux (SDK `v3.7.14_build_55`); validador Steinberg: 47
  testes aprovados, 0 falhas.
- Trecho real de 10 s (faixa, 60–70 s), CLI, `--autogain off`: mesmo nº de frames;
  ΔRMS −0,37/−0,11 dB; atraso de conteúdo +40 amostras (0,21 ms).

## Limitações que esta mudança NÃO resolve

1. **Transientes largos:** energia de clique −3,03 dB (estrutural; reproduzido:
   −3,03/−3,11 dB). Subida 10–90% 3,31→9,10 ms na faixa (registrado no handoff,
   não refeito). Pré-eco da rajada tonal varia com o alinhamento (ver gate E).
2. **Bins 0 e N/2** gravados como magnitude (`pitch_engine.cpp`): DC ≈1e-4 e resíduo
   no nulo (reproduzido: −42,6 dB re pico² no 2º clique em f=1,0; nulo em banda cheia
   −76,2 dB é do handoff, não refeito). **Não tocado nem testado**; o gate A mede só 1–60 kHz e não cobre esses bins.
3. **Pico real da faixa +0,96 dBFS** (handoff): com `Protecao de Ganho Automatica`
   **ligada** (default: desligada), o teto reduz ≈1,96 dB. É política de teto.
4. **`PPS_TWIST` (sinal do twist):** desligado; o efeito absoluto em música não fechou.

## Não verificado

- Build **MSVC/Windows** (só o CI faz) e compilação dos testes novos em MSVC.
  Se um teste falhar no CI, o workflow para antes de "Locate"/"Upload": sem artefato.
- **Audacity** e o tratamento de latência pelo host (README diz "compensada
  automaticamente"; não conferi). Mudança de fator durante o render (automação) e
  rotação "congelada" ao voltar a f=1,0: não testadas.
- Mudar `Processamento` (Alta Precisão↔Eficiente) muda a latência; não há
  `restartComponent` em `src/plugin/` (grep), então o host pode não ser avisado.
  Por leitura de código, não testado em host.
