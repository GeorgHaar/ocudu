# Ideales Richtdiagramm aus PDSCH-Portsignalen

Die Python-Werkzeuge ändern keine C++-Dateien. Benötigt werden Python 3, NumPy,
Matplotlib und für das Fenster ein GUI-Backend, zum Beispiel Tk.

## Sofort mit Abstandsreglern ausprobieren

Im Projektverzeichnis:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --demo
```

Die ausdrücklich als Demo markierten Daten bilden zwei räumliche Positionen und
zwei orthogonale Polarisationen ab. Ein Beispiel mit vier Positionen:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --demo --n1 4
```

`dx` und `dy` werden in Wellenlängen angegeben. Bei 3,5 GHz entspricht
`0.5 lambda` ungefähr 4,28 cm. Bei `N2=1` hat `dy` keine Wirkung.
Die Anzeige rechnet mit `--frequency-ghz 3.5` auch in Zentimeter um.
Diese Modellfrequenz ist unabhängig von der gNB-Konfiguration; bei Bedarf
die tatsächliche Trägerfrequenz übergeben.

## Drehbarer 3D-Viewer

Mit `--3d` öffnet sich zusätzlich eine 3D-Ansicht. Beide Fenster verwenden
denselben Empfänger und dieselbe Kovarianz; die Abstandsregler sind synchron.
Die Ansicht lässt sich mit der Maus drehen und zoomen. Die Kameraposition bleibt
bei neuen IQ-Blöcken und beim Verändern der Abstände erhalten.

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --3d
```

Mit der vorhandenen Aufzeichnung:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --3d --capture /tmp/ocudu_pdsch_iq.bin
```

Die Länge des Radius zeigt die relative Feldamplitude `sqrt(P/Pmax)`, die Farbe
die relative Leistung in dB. Es wird die ganze Kugel dargestellt. Das ideale
Modell aus isotropen Elementen besitzt auch rückwärtige Abstrahlung; eine reale
Elementcharakteristik mit Rückdämpfung ist hier nicht modelliert.

Ein statischer Export mit `--3d --save-preview /tmp/pattern.png` erzeugt
`pattern.png` und zusätzlich `pattern_3d.png`.

## Echte Daten aus der laufenden GDB-Sitzung

Zuerst im normalen Terminal den Empfänger starten:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --n1 2 --n2 1
```

Dann am GDB-Prompt:

```gdb
source /home/playbox/CLionProjects/ocudu/docs/uml/precoding_tests/iq_stream_gdb.py
iq-stream-start
continue
```

Der Abgriff liegt in `resource_grid_mapper_impl.cpp:183`, **nach** dem vollständigen
Precoding des Blocks über alle Ports. Er liest maximal 64 gemeinsame REs je Port,
einschließlich cbf16-Rundung, und sendet höchstens alle 200 ms einen UDP-Block an
`127.0.0.1:19090`. Andere aktive Breakpoints können den Lauf weiterhin anhalten.
Die eingestellte Bedingung erfasst zwei Layer, passend zur aktuellen Test-UE.
Bei einer anderen Layeranzahl muss die Bedingung des neu angelegten Breakpoints
in GDB angepasst werden.

Zum Stoppen Strg+C drücken und dann:

```gdb
iq-stream-stop
```

GDB hält die gNB bei jedem passenden Breakpoint kurz an, auch wenn nicht jeder
Treffer exportiert wird. Die Bildwiederholrate des Viewers beträgt bis zu 5 Hz.
Das ist eine Diagnoseansicht und kein verlustfreier Stream mit voller Datenrate.
Mehrere Zellen oder wechselnde PMIs werden derzeit nicht getrennt gruppiert;
für diesen Prototyp eine Zelle, eine Test-UE und einen festen PMI verwenden.

## Einen fertigen Block speichern

```gdb
iq-capture-once /tmp/ocudu_pdsch_iq.bin
continue
```

GDB hält am fertig berechneten Block an. Anschließend lässt sich das Modell
beliebig oft und ohne laufende gNB untersuchen:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --capture /tmp/ocudu_pdsch_iq.bin
```

Eine statische Vorschau ohne GUI:

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --capture /tmp/ocudu_pdsch_iq.bin --save-preview /tmp/array_pattern.png
```

## Physikalisches Modell

- Die Portreihenfolge entspricht `pol*(N1*N2) + ix*N2 + iy`.
- Die beiden Polarisationen liegen an denselben räumlichen Positionen. Ihre
  Leistungen werden für ideale orthogonale Polarisationen addiert.
- Aus synchronen Portblöcken `Y` wird die Kovarianz `R = Y @ Y.conj().T / N`
  gebildet und über empfangene Blöcke geglättet.
- Für eine Beobachtungsrichtung `u` verwendet das Sendemodell die Phasen
  `a_p = exp(+j*2*pi*r_p*u)`, mit Positionen `r_p` in Wellenlängen.
  Je Polarisation wird `a @ R @ a.conj().T` ausgewertet.
- `+z` ist die Flächennormale. Positive Azimutwinkel zeigen Richtung `+x`,
  positive Elevation Richtung `+y`. Gezeigt wird der vordere Halbraum.
- Der Regler verschiebt die **modellierten** Elemente. Die gNB-Gewichte bleiben
  unverändert. Ein veränderter Abstand verändert dadurch auch die Beamrichtung.
- Das Modell verwendet ideale isotrope Elemente ohne Kopplung, Kabelverluste
  oder Kalibrierfehler. Es zeigt relative Leistung, keinen absoluten Antennengewinn.
- Das aktuelle 2x1-Panel hat nur zwei Positionen je Polarisation; daraus entsteht
  kein typisches schmales Richtdiagramm eines großen Arrays. Größere Abstände
  können zusätzliche starke Keulen erzeugen.

Für eine spätere Anwendung mit voller Datenrate ist der gleiche Datenvertrag
nutzbar: ein C++-Abgriff kopiert ausgewählte fertige Portblöcke in einen begrenzten
Ringpuffer; ein separater Thread exportiert sie ohne Blockieren des PHY-Workers.
Bei Überlast werden Diagnoseblöcke verworfen. Diesen C++-Abgriff enthalten die
Werkzeuge ausdrücklich noch nicht.

Referenzen:

- [Matplotlib Slider](https://matplotlib.org/stable/gallery/widgets/slider_demo.html)
- [Matplotlib 3D-Flächen](https://matplotlib.org/stable/plot_types/3D/surface3d_simple.html)
- [NumPy frombuffer](https://numpy.org/doc/stable/reference/generated/numpy.frombuffer.html)
- [Array- und Elementdiagramme](https://www.mathworks.com/help/phased/ug/element-and-array-radiation-patterns-and-responses.html)

## Prüfung

```bash
python3 docs/uml/precoding_tests/iq_live_viewer.py --self-test
```

Geprüft werden bf16-Dekodierung, Paketlänge, Beamrichtung, Einfluss des
Elementabstands, Leistungsaddition orthogonaler Polarisationen sowie
3D-Kugel-, Nullleistungs- und Vorder-/Rückseitenfälle.
