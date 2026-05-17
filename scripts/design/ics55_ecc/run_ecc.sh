#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

ECC_BINARY="${ECC_BINARY:-${SCRIPT_DIR}/ecc}"
WORKSPACE="${WORKSPACE_HOME:-gcd}"

if [[ ! -x "${ECC_BINARY}" && -x "${REPO_ROOT}/bin/ecc" ]]; then
  ECC_BINARY="${REPO_ROOT}/bin/ecc"
fi

if [[ ! -x "${ECC_BINARY}" ]]; then
  echo "ecc binary is not executable: ${ECC_BINARY}" >&2
  echo "Set ECC_BINARY or build ${REPO_ROOT}/bin/ecc first." >&2
  exit 1
fi

usage() {
  cat <<EOF
Usage:
  $(basename "$0") [WORKSPACE]
  $(basename "$0") [all|STEP] [WORKSPACE]

Examples:
  $(basename "$0")
  $(basename "$0") gcd
  $(basename "$0") all gcd
  $(basename "$0") CTS gcd
  $(basename "$0") route /path/to/gcd

Steps:
  Floorplan fixFanout place CTS legalization route drc filler
EOF
}

STEP="all"
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ge 1 ]]; then
  case "$1" in
    all|Floorplan|fixFanout|place|CTS|legalization|route|drc|filler)
      STEP="$1"
      shift
      ;;
    *)
      WORKSPACE="$1"
      shift
      if [[ $# -ne 0 ]]; then
        echo "too many arguments" >&2
        usage >&2
        exit 2
      fi
      ;;
  esac
fi

if [[ $# -ge 1 ]]; then
  WORKSPACE="$1"
  shift
fi

if [[ $# -ne 0 ]]; then
  echo "too many arguments" >&2
  usage >&2
  exit 2
fi

cd "${SCRIPT_DIR}"

case "${STEP}" in
  all)
    exec "${ECC_BINARY}" -script run_workspace.tcl "${WORKSPACE}"
    ;;
  Floorplan|fixFanout|place|CTS|legalization|route|drc|filler)
    exec "${ECC_BINARY}" -script "steps/${STEP}.tcl" "${WORKSPACE}"
    ;;
  *)
    echo "unsupported step: ${STEP}" >&2
    usage >&2
    exit 2
    ;;
esac
