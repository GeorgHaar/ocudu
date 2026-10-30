# UML-Explorer für die Precoding-Unittests

Erzeugt aus `tests/unittests/ran/precoding` eine lokale Webseite mit Sequenz- und Klassendiagrammen, dem Originalcode
und der Doxygen-Dokumentation. Ein Klick auf ein Diagrammelement zeigt die zugehörige Quellcodestelle.

## Voraussetzungen

- Ein konfigurierter Build mit `compile_commands.json`, standardmäßig `cmake-build-debug`.
- Die Test-Programme gebaut, zum Beispiel `cmake --build cmake-build-debug --target precoding_codebooks_test`.
  Ohne sie entfallen die Lauf-Zähler der parametrisierten Tests und die gdb-Validierung.
- [clang-uml](https://github.com/bkryza/clang-uml), Java und ein PlantUML-JAR (`PLANTUML_JAR` oder das JAR des
  CLion-Plugins "PlantUML Integration").
- Optional Doxygen mit Graphviz für die Dokumentation und gdb für die Validierung der Aufrufketten.

## Aufruf

```bash
python3 docs/uml/precoding_tests/generate_uml.py
xdg-open cmake-build-debug/clang_uml/precoding_tests/viewer/index.html
```

Ein vollständiger Lauf dauert etwa zehn Minuten und braucht bis zu 3 GB Arbeitsspeicher. Optionen:

| Option | Wirkung |
|---|---|
| `--build-dir DIR` | anderes Build-Verzeichnis |
| `--threads N` | clang-uml-Threads, Standard 4; jeder Thread braucht mehrere hundert MB |
| `--reuse` | clang-uml und Doxygen überspringen und die vorhandene Ausgabe neu verarbeiten |
| `--no-gdb` | gdb-Validierung überspringen |
| `--memory-limit GB` | Adressraum je clang-uml-Prozess, Standard 6 GB; ein Diagramm, das mehr braucht, bleibt beim ersten Durchgang |
| `--all-calls` | triviale Getter zeigen und wiederholte Aufrufe nicht zusammenfassen |

## Was erzeugt wird

- Eine Startseite mit allen Test-Programmen und ihren Quelldateien.
- Je Programm die Aufrufkette von `main()` über gtest bis zu den Testfällen, inklusive Fixture-`SetUp()` und
  `INSTANTIATE_TEST_SUITE_P`.
- Je Testfall ein Sequenzdiagramm von der Testfunktion in die Bibliotheken.
- Drei Klassendiagramme: Precoding-Typen, Beamforming- und PHY-Typen, Test-Fixtures.
- Doxygen-Seiten mit Aufruf- und Aufrufergraphen des beteiligten Codes.

## Validierung mit gdb

Jeder Testfall läuft einmal unter gdb. Ein Breakpoint auf jeder Funktion der beteiligten Verzeichnisse zeichnet die
dynamische Aufrufkette auf. Funktionen, die nur über Code außerhalb des Diagramms erreicht werden, zum Beispiel über
`std::visit`, über gtest-Assertions wie `ASSERT_EQ` oder über vom Compiler erzeugte Member, werden als zusätzliche
Startpunkte in das Diagramm aufgenommen. Das Ergebnis steht in `gdb_validation.json` und in der Webseite bei jedem
Testfall.

## Dateien

- `generate_uml.py`: der Generator.
- `gdb_trace.py`: der gdb-Tracer, den der Generator aufruft.
- `viewer.html`: die Vorlage der Webseite.

Die Ausgabe liegt unter `<Build-Verzeichnis>/clang_uml/precoding_tests` und gehört nicht ins Repository.
