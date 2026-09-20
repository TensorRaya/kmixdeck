# `kmixdeck` — command-line reference

🔴 **Diese Seite ist umgezogen.** Die CLI-Referenz steht seit CL-1 in
[`kmixdeck.md`](kmixdeck.md) und ist nach **man-pages(7)** aufgebaut. Aus dieser
einen Quelle entstehen drei Ausgaben:

| Ausgabe | Wie | Wofür |
|---|---|---|
| `man kmixdeck` | `docs/generiere-doku.py man` → `kmixdeck.1` (CMake, nicht eingecheckt) | Die Vollreferenz auf dem System |
| `kmixdeck --help` | `docs/generiere-doku.py help` | Eine Seite, nur NAME/SYNOPSIS/EXAMPLES/OPTIONS |
| diese Datei | Zeiger auf die Quelle | Damit kein zweiter Text entsteht, der driftet |

**Warum kein zweiter Volltext hier:** Doku, die an zwei Stellen steht, ist nach
dem dritten Commit an einer Stelle falsch — und niemand weiß, an welcher. Ein
Duplikat zu pflegen kostet mehr als es einbringt, also gibt es keins.

## Ändern

```sh
$EDITOR docs/kmixdeck.md          # der einzige Ort, an dem Text stehen darf
python3 docs/generiere-doku.py pruefen   # Abschnitte + Reihenfolge nach man-pages(7)
python3 docs/generiere-doku.py man | groff -man -Tutf8 -ww -z   # muss warnungsfrei sein
man -l <(python3 docs/generiere-doku.py man)                    # so sieht es aus
```

`pruefen` lehnt erfundene Abschnittsnamen ab: erlaubt sind nur NAME, SYNOPSIS,
DESCRIPTION, EXAMPLES, OPTIONS, COMMANDS, REFERENCES, EXIT STATUS, FILES,
SEE ALSO, NOTES — in dieser Reihenfolge. Themen, die keinen eigenen
man-Abschnitt haben (Levels, Slugs, Port-Referenzen), sind Unterabschnitte
(`##` → `.SS`) unter REFERENCES; Scripting-Hinweise stehen in NOTES.

`kmixdeck.1` wird **nicht eingecheckt**, sondern von CMake bei jedem Build aus
`docs/kmixdeck.md` erzeugt (Target `manpage`). Eine eingecheckte generierte
Datei ist eine Einladung, sie von Hand zu ändern.
