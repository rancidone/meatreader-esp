#!/usr/bin/env bash
set -Eeuo pipefail

# Codex Cloud bootstrap for meatreader-esp.
#
# Security posture for public-repo safety:
# - can skip system package installation by default (least privilege)
# - uses pinned ESP-IDF version by default; configurable when needed

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$REPO_ROOT/firmware"
UI_DIR="$REPO_ROOT/thermometer-ui"
IDF_PATH_DEFAULT="/opt/esp-idf"
IDF_VERSION_DEFAULT="v5.2.2"
IDF_TARGET_DEFAULT="esp32"
IDF_REPO_URL="https://github.com/espressif/esp-idf.git"

: "${IDF_PATH:=$IDF_PATH_DEFAULT}"
: "${IDF_VERSION:=$IDF_VERSION_DEFAULT}"
: "${IDF_TARGET:=$IDF_TARGET_DEFAULT}"
: "${PREFERRED_PYTHON:=python3.13}"
: "${INSTALL_SYSTEM_PACKAGES:=0}"   # 1 to run apt-get install
: "${INSTALL_UI_DEPS:=1}"            # 1 to run npm install

PYTHON_SHIM_DIR=""

log() {
  printf '\n[%s] %s\n' "$(date +'%Y-%m-%d %H:%M:%S')" "$*"
}

warn() {
  printf '\n[%s] WARNING: %s\n' "$(date +'%Y-%m-%d %H:%M:%S')" "$*" >&2
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}
require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

cleanup() {
  if [[ -n "$PYTHON_SHIM_DIR" && -d "$PYTHON_SHIM_DIR" ]]; then
    rm -rf "$PYTHON_SHIM_DIR"
  fi
}

select_python() {
  local -a candidates
  local python_cmd=""
  local python_path=""

  candidates=("$PREFERRED_PYTHON" python3.14 python3.13 python3.12 python3.11 python3.10 python3.9 python3 python)

  for python_cmd in "${candidates[@]}"; do
    command -v "$python_cmd" >/dev/null 2>&1 || continue
    if "$python_cmd" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)'; then
      python_path="$(command -v "$python_cmd")"
      break
    fi
  done

  [[ -n "$python_path" ]] || die "Could not find a Python 3.9+ interpreter in PATH"

  # Ensure ESP-IDF's detect_python.sh sees this interpreter first when checking "python3".
  PYTHON_SHIM_DIR="$(mktemp -d)"
  ln -sf "$python_path" "$PYTHON_SHIM_DIR/python3"
  export PATH="$PYTHON_SHIM_DIR:$PATH"

  log "Selected Python for ESP-IDF: $("$python_path" --version 2>&1) ($python_path)"
}

validate_inputs() {
  [[ "$IDF_PATH" = /* ]] || die "IDF_PATH must be an absolute path (got: $IDF_PATH)"
  [[ "$IDF_VERSION" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || {
    die "IDF_VERSION must look like vX.Y or vX.Y.Z (got: $IDF_VERSION)"
  }
  [[ "$IDF_TARGET" =~ ^[a-z0-9_]+$ ]] || die "IDF_TARGET has invalid format: $IDF_TARGET"
}

install_apt_packages() {
  local -a apt_cmd

  if [[ "$INSTALL_SYSTEM_PACKAGES" != "1" ]]; then
    log "Skipping OS package install (set INSTALL_SYSTEM_PACKAGES=1 to enable)"
    return
  fi

  command -v apt-get >/dev/null 2>&1 || die "apt-get not available, cannot install packages"
  if [[ "$(id -u)" -eq 0 ]]; then
    apt_cmd=(apt-get)
  elif command -v sudo >/dev/null 2>&1; then
    apt_cmd=(sudo apt-get)
  else
    die "apt-get requires elevated privileges and sudo is unavailable"
  fi

  log "Installing OS packages required for ESP-IDF + this repo"
  export DEBIAN_FRONTEND=noninteractive
  "${apt_cmd[@]}" update -y
  "${apt_cmd[@]}" install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0 \
    python3 \
    python3-venv \
    python3-pip \
    nodejs \
    npm
}

install_esp_idf() {
  require_cmd git
  require_cmd python3

  if [[ -d "$IDF_PATH/.git" ]]; then
    log "ESP-IDF already present at $IDF_PATH"
  else
    log "Cloning ESP-IDF ($IDF_VERSION) into $IDF_PATH"
    mkdir -p "$(dirname "$IDF_PATH")"
    git clone --depth 1 --branch "$IDF_VERSION" "$IDF_REPO_URL" "$IDF_PATH"
  fi

  [[ -x "$IDF_PATH/install.sh" ]] || die "missing installer: $IDF_PATH/install.sh"

  log "Running ESP-IDF tool installer for target: $IDF_TARGET"
  unset IDF_PYTHON_ENV_PATH
  "$IDF_PATH/install.sh" "$IDF_TARGET"
}

validate_idf_py() {
  local export_log
  local export_rc

  [[ -f "$IDF_PATH/export.sh" ]] || die "expected export script missing: $IDF_PATH/export.sh"

  log "Validating idf.py availability (tolerating optional tool issues)"
  export_log="$(mktemp)"

  unset IDF_PYTHON_ENV_PATH
  set +e
  # shellcheck disable=SC1090
  source "$IDF_PATH/export.sh" >"$export_log" 2>&1
  export_rc=$?
  set -e

  if [[ "$export_rc" -ne 0 ]]; then
    warn "ESP-IDF export reported issues. This is often caused by optional tools in minimal containers."
    warn "Continuing with fallback idf.py validation."
  fi

  if command -v idf.py >/dev/null 2>&1; then
    if idf.py --version; then
      rm -f "$export_log"
      return
    fi
    warn "idf.py is on PATH but failed to run."
  fi
  if [[ -x "$IDF_PATH/tools/idf.py" ]]; then
    if IDF_PATH="$IDF_PATH" "$IDF_PATH/tools/idf.py" --version; then
      rm -f "$export_log"
      return
    fi
    warn "Fallback idf.py at $IDF_PATH/tools/idf.py failed to run."
  fi

  echo "ERROR: idf.py is not available after setup." >&2
  if [[ "$export_rc" -ne 0 ]]; then
    echo "Last lines from export.sh output:" >&2
    tail -n 20 "$export_log" >&2 || true
  fi
  rm -f "$export_log"
  exit 1
}

install_ui_deps() {
  if [[ "$INSTALL_UI_DEPS" != "1" ]]; then
    log "Skipping npm install (set INSTALL_UI_DEPS=1 to enable)"
    return
  fi

  if [[ -f "$UI_DIR/package-lock.json" ]]; then
    log "Installing thermometer-ui dependencies with npm ci"
    (cd "$UI_DIR" && npm ci --ignore-scripts)
  elif [[ -f "$UI_DIR/package.json" ]]; then
    log "package-lock.json missing; falling back to npm install"
    (cd "$UI_DIR" && npm install --ignore-scripts)
  else
    log "No UI package manifest found at $UI_DIR; skipping npm install"
  fi
}

summary() {
  cat <<MSG

Bootstrap complete.

Defaults are conservative for public-repo safety.
Use these to enable more automation:
  INSTALL_SYSTEM_PACKAGES=1

Next steps:
  1) If needed, install OS deps: INSTALL_SYSTEM_PACKAGES=1 ./scripts/codex-startup.sh
  2) Enter firmware dir: cd "$FIRMWARE_DIR"
  3) Source IDF env: source "$IDF_PATH/export.sh"
  4) Confirm idf.py: which idf.py && idf.py --version
  5) Build firmware: idf.py build
  6) Run UI dev server: cd "$UI_DIR" && npm run dev

Overrides:
  IDF_PATH    (default: $IDF_PATH_DEFAULT)
  IDF_VERSION (default: $IDF_VERSION_DEFAULT)
  IDF_TARGET  (default: $IDF_TARGET_DEFAULT)
MSG
}

main() {
  trap cleanup EXIT
  log "Starting Codex Cloud bootstrap for meatreader-esp"
  validate_inputs
  select_python
  install_apt_packages
  install_esp_idf
  validate_idf_py
  install_ui_deps
  summary
}

main "$@"
