#!/bin/sh
# KDE convention: extracts every i18n()/i18nc()/i18np() string into po/kmixdeck.pot (UX-5).
# Run from the repo root: sh Messages.sh   — needs gettext (xgettext). The German catalog is po/de/kmixdeck.po.
$EXTRACT_TR_STRINGS `find src -name '*.cpp' -o -name '*.h' -o -name '*.qml'` -o $podir/kmixdeck.pot
