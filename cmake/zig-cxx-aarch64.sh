#!/bin/sh
exec /opt/homebrew/bin/zig c++ -target aarch64-linux-gnu.2.38 "$@"
