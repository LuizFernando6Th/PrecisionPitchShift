# Licenças

* Todo o código-fonte deste repositório (`src/`, `tests/`, `tools/`,
  `CMakeLists.txt`, documentação) é distribuído sob a **Licença MIT**
  — ver `MIT.txt`.
* O **VST3 SDK da Steinberg**, necessário apenas para compilar o plugin
  `PrecisionPitchShift.vst3`, **NÃO** está incluído neste repositório e tem
  licença própria (Steinberg VST3 License Agreement — dupla licença
  GPLv3 / proprietária). Leia `LICENSE.txt` / `VST3_License_Agreement.pdf`
  da sua cópia do SDK antes de distribuir binários. Ver README § Compilação.
* Nenhuma outra biblioteca de DSP de terceiros é usada: a FFT, o vocoder de
  fase e todas as ferramentas são implementação própria (MIT), justamente
  para que o comportamento em toda a banda até Nyquist seja auditável e não
  exista nenhum filtro fixo de 20 kHz escondido em dependência externa.
