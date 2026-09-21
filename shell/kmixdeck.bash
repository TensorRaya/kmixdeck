# SPDX-License-Identifier: GPL-3.0-or-later
# CL-7: bash completion for kmixdeck.
#
# Absichtlich duenn: dieses Skript kennt KEINE Kommandonamen. Es fragt
# `kmixdeck complete <index> <worte...>`, und die Kandidaten kommen aus derselben
# Tabelle, die das CLI selbst zum Dispatch benutzt (KOMMANDO_NAMEN) plus dem
# generierten Hilfetext (docs/kmixdeck.md).
#
# Warum das so sein MUSS: eine Completion mit eigener Liste driftet lautlos.
# `tree` und `patch` kamen am 2026-09-21 dazu; ein Skript mit Handliste haette sie
# nicht gekannt, und kein Test waere rot geworden — fehlende Vervollstaendigung ist
# unsichtbar. Hier kann sie nicht driften: es gibt nur eine Quelle.
#
# Ohne laufenden Daemon bleiben die statischen Kandidaten (Kommandos,
# Unterkommandos); Slugs, Geraete und Szenen fallen weg. Das ist die Anforderung,
# nicht ein Mangel.

_kmixdeck() {
    local IFS=$'\n'
    # COMP_CWORD zaehlt ab 0 auf COMP_WORDS, wobei [0] das Programm ist — genau die
    # Zaehlweise, die `kmixdeck complete` erwartet.
    COMPREPLY=($(compgen -W "$(kmixdeck complete "$COMP_CWORD" "${COMP_WORDS[@]}" 2>/dev/null)" \
                         -- "${COMP_WORDS[COMP_CWORD]}"))
}

complete -F _kmixdeck kmixdeck
