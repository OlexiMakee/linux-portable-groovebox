#!/bin/sh
if command -v zig >/dev/null 2>&1; then
    exec zig c++ -target aarch64-linux-gnu.2.38 "$@"
else
    exec /opt/homebrew/bin/zig c++ -target aarch64-linux-gnu.2.38 "$@"
fi
