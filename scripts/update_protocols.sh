#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO_DIR="${ROOT_DIR}/protocols"
PROTO_GEN_DIR="${PROTO_DIR}/generated"
WAYLAND_SCANNER_BIN="${WAYLAND_SCANNER:-wayland-scanner}"
FETCH_XML=0

usage() {
    cat <<'EOF'
Usage: scripts/update_protocols.sh [--fetch]

Regenerates the client headers and private-code sources for the vendored
Wayland protocols. By default the existing XML descriptions under protocols/
are used. Pass --fetch to download the latest XML files from their upstream
projects before regenerating the sources.
EOF
}

if [[ "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "${1:-}" == "--fetch" ]]; then
    FETCH_XML=1
    shift
fi

mkdir -p "${PROTO_DIR}" "${PROTO_GEN_DIR}"

declare -A PROTOCOL_URLS=(
    ["wlr-foreign-toplevel-management-unstable-v1"]="https://raw.githubusercontent.com/swaywm/wlroots/master/protocol/wlr-foreign-toplevel-management-unstable-v1.xml"
    ["hyprland-toplevel-export-v1"]="https://raw.githubusercontent.com/hyprwm/hyprland-protocols/main/protocols/hyprland-toplevel-export-v1.xml"
)

if [[ "${FETCH_XML}" -eq 1 ]]; then
    command -v curl >/dev/null 2>&1 || {
        echo "curl is required to fetch protocol XML descriptions" >&2
        exit 1
    }
    for name in "${!PROTOCOL_URLS[@]}"; do
        url=${PROTOCOL_URLS[$name]}
        dest="${PROTO_DIR}/${name}.xml"
        echo "Downloading ${name}.xml"
        curl -fsSL "${url}" -o "${dest}"
    done
fi

command -v "${WAYLAND_SCANNER_BIN}" >/dev/null 2>&1 || {
    echo "wayland-scanner is required to (re)generate protocol sources" >&2
    exit 1
}

shopt -s nullglob
for xml in "${PROTO_DIR}"/*.xml; do
    base="$(basename "${xml}" .xml)"
    echo "Generating sources for ${base}"
    "${WAYLAND_SCANNER_BIN}" client-header "${xml}" "${PROTO_GEN_DIR}/${base}-client-protocol.h"
    "${WAYLAND_SCANNER_BIN}" private-code "${xml}" "${PROTO_GEN_DIR}/${base}-protocol.c"
done

