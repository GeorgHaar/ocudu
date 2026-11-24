# Änderungsprotokoll: Beamforming-Quantisierung

Arbeitsstand im Arbeitsbaum, nicht committet. Basis ist Commit `409af2854a` (Add precoding docs and live IQ viewer).

## 2026-11-20

### Gewichtsquantisierung (Type I Single-Panel)

- `include/ocudu/ran/precoding/precoding_weight_quantization.h`, `lib/ran/precoding/precoding_weight_quantization.cpp` (neu):
  Struktur `precoding_weight_quantization` mit `phase_bits` und `iq_fraction_bits`.
  `quantize_phase()`: Phase auf 2^b Stufen, Betrag bleibt, Halbschritte runden nach oben, Null bleibt Null.
  `quantize_fixed_point()`: Real- und Imaginärteil als Vorzeichen plus M Nachkommabits, Schritt 2^-M,
  Sättigung bei ±(1 - 2^-M), Rundung Ties-to-Even (entspricht `FloatFormat(E=0, M)` in Slim).
  `quantize()`: erst Phase, dann I/Q, jede Stufe einzeln abschaltbar.
- `lib/ran/CMakeLists.txt`: neue Quelle eingetragen.
- `lib/ran/precoding/type1_sp/precoding_codebooks.{h,cpp}`: dritter Parameter `quantization` an `make_type1_sp_mode1()`,
  Aufruf von `quantize()` nach der Skalierung. Studienmarker `[Q1]` bis `[Q9]` als Kommentare an jeder Rechenstelle.
- `include/ocudu/ran/precoding/precoding_codebooks.h`, `lib/ran/precoding/precoding_codebooks.cpp`: Parameter durch
  `make_precoding()` und den Visitor `precoding_calculator` gereicht; 2-Port und Type II ignorieren ihn.
- `include/ocudu/fapi_adaptor/precoding_codebook_generator.h`, `lib/fapi_adaptor/precoding_codebook_generator.cpp`:
  Parameter bis `generate_pdsch_sp_type1()` und `make_composite()`; die Beam-Grid-Zerlegung bleibt unquantisiert.
- `lib/du/du_low/o_du_low_factory.cpp`: Experiment-Einstellung `weight_quantization`, aktuell `.phase_bits = 2`,
  `.iq_fraction_bits = std::nullopt`. Kein YAML-Schlüssel.
- `tests/unittests/ran/precoding/precoding_codebooks_test.cpp`: sechs neue Tests
  (`QuantizePhase_Coefficient`, `Type1SinglePanelMode1_PhaseQuantizationOnGridIsExact`,
  `Type1SinglePanelMode1_PhaseQuantizationTwoBits`, `QuantizeFixedPoint_Coefficient`,
  `Type1SinglePanelMode1_FixedPointTwoFractionBits`, `Type1SinglePanelMode1_FixedPointControl`).
  Suite: 38 Tests bestanden. `precoding_codebook_generator_test`: 6 bestanden.

### Laufumgebung

- `lib/phy/upper/upper_phy_factories.cpp`: Channel-Precoder-Fabrik von `"auto"` auf `"generic"`, damit
  `channel_precoder_generic` statt der AVX2-Variante läuft (keine Konfigurationsoption vorhanden).
- `configs/testmode_4x4.yml` (neu): Dummy-RU ohne Core, 4 DL/UL-Antennen, Phantom-UE mit `ri: 2`, `i_1_1: 3`,
  `i_2: 1`, `ru_dummy.time_scaling: 40` wegen des Debug-Builds.
- `docs/uml/precoding_tests/iq_stream_gdb.py`: Zwei-Layer-Filter in `stop()` statt `self.condition`; gdb ignoriert
  eine Bedingung auf einem Breakpoint mit Python-`stop()`.

### Dokumentation

- `lib/ran/precoding/explainations/quantisierung_kette.pptx` (neu): Folie mit der Kette in Quellcode-Reihenfolge,
  Reihe 1 Gewichte erzeugen (Q7, Layer-Auswahl, Schleife Q1–Q6, Q8, Q9), Reihe 2 Datenpfad (ci8, Produkt, Summe,
  cbf16, IFFT, ci16, DAC). Farben: fest / eingebaut / theoretisch möglich / keine Rechnung.
- `lib/ran/precoding/explainations/quantisierung_kette_vorschau_4k.png` (neu): 3840 × 2160, Näherungsdarstellung
  aus den Shape-Geometrien, da kein LibreOffice installiert ist.

### Verworfen

- Phasenindex-Form (`type1_sp_phase_index_matrix`, `make_type1_sp_mode1_phase_indices()`, Helfer
  `select_type1_sp_layers()`, zwei Tests): eingebaut, über 2624 Matrizen validiert, auf Wunsch wieder entfernt.

### Befunde

- Type-I-Phasen liegen auf einem Raster von 45° (2×1, 2×2) bzw. 22,5° (4×1); 3 bzw. 4 Bit Phase reproduzieren die
  Matrizen exakt, darunter verschiebt sich die Keule (i_1_1 = 1: 2 Bit drehen 45° auf 90°, Hauptkeule von −14,5°
  nach −30°).
- Viewer (`iq_live_viewer.py`) und Slim-Referenz liefern für denselben PMI identische Schnitte und Heatmaps
  (Abweichung 0,000 dB); abweichende Bilder kommen aus GUI-Einstellungen (N1, W*, Winkelbereich, dB-Untergrenze).
- Feste Quantisierungen im Datenpfad: Symbole `ci8_t`, Resource Elements `cbf16_t`, Zeitsamples `ci16_t`, DAC 12 Bit.

### Außerhalb des Repos (Scratchpad der Sitzung)

- `pptx/build.js`, `pptx/render4k.py`: Erzeugung der Folie und der Vorschau.
- `compare_heatmaps.py`: Slim gegen Viewer für einen Type-I-PMI.
- `block_2bit.ociq`: gdb-Capture eines Zwei-Layer-PDSCH-Blocks mit 2-Bit-Phasen.
