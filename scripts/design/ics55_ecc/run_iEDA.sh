#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

WORKSPACE_HOME="${WORKSPACE_HOME:-${REPO_ROOT}/scripts/design/ics55_gcd_workspace/home}"
IEDA_BINARY="${IEDA_BINARY:-${REPO_ROOT}/bin/iEDA}"

if [[ ! -x "${IEDA_BINARY}" && -x "${REPO_ROOT}/scripts/design/sky130_gcd/iEDA" ]]; then
  IEDA_BINARY="${REPO_ROOT}/scripts/design/sky130_gcd/iEDA"
fi

if [[ ! -x "${IEDA_BINARY}" ]]; then
  echo "iEDA binary is not executable: ${IEDA_BINARY}" >&2
  echo "Set IEDA_BINARY or build ${REPO_ROOT}/bin/iEDA first." >&2
  exit 1
fi

exec "${IEDA_BINARY}" -script "${SCRIPT_DIR}/run_workspace.tcl" "${WORKSPACE_HOME}"
