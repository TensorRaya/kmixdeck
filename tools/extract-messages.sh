#!/bin/sh
# Standalone version of Messages.sh for developers without KDE's scripty: writes po/kmixdeck.pot and merges it into
# every po/<lang>/kmixdeck.po (msgmerge keeps existing translations, marks changed ones fuzzy).
set -e
cd "$(dirname "$0")/.."
find src -name '*.cpp' -o -name '*.h' -o -name '*.qml' | sort > /tmp/kmixdeck-sources.txt
xgettext --from-code=UTF-8 -C --kde \
  -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 \
  -kI18N_NOOP:1 -kI18NC_NOOP:1c,2 \
  --package-name=kmixdeck --msgid-bugs-address=https://github.com/TensorRaya/kmixdeck/issues \
  -L C++ -f /tmp/kmixdeck-sources.txt -o po/kmixdeck.pot
sed -i 's/charset=CHARSET/charset=UTF-8/' po/kmixdeck.pot
for po in po/*/kmixdeck.po; do
  [ -f "$po" ] && msgmerge --quiet --update --backup=none "$po" po/kmixdeck.pot
done
echo "strings: $(grep -c '^msgid ' po/kmixdeck.pot)"
