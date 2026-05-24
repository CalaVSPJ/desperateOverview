#!/usr/bin/env bash
# Thin wrapper around `desperateOverview --toggle`.
#
# Exists for keybinding setups that prefer to invoke a script rather than
# the binary directly. Forwards all positional arguments verbatim.
#
# For per-user guards (e.g. only show on a specific monitor, only between
# certain hours), copy this file and add your own checks before the exec.
exec desperateOverview --toggle "$@"
