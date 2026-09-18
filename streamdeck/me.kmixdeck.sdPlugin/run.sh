#!/bin/sh
# OpenAction entrypoint: the server runs this with -port/-pluginUUID/-registerEvent/-info. Python 3 + websockets.
cd "$(dirname "$0")" && exec python3 plugin.py "$@"
