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
: "${INSTALL_SYSTEM_PACKAGES:=0}"   # 1 to run apt-get install
: "${INSTALL_UI_DEPS:=1}"            # 1 to run npm ci

log() {
  printf '\n[%s] %s\n' "$(date +'%Y-%m-%d %H:%M:%S')" "$*"
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

validate_inputs() {
  [[ "$IDF_PATH" = /* ]] || die "IDF_PATH must be an absolute path (got: $IDF_PATH)"
  [[ "$IDF_VERSION" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || {
    die "IDF_VERSION must look like vX.Y or vX.Y.Z (got: $IDF_VERSION)"
  }
  [[ "$IDF_TARGET" =~ ^[a-z0-9_]+$ ]] || die "IDF_TARGET has invalid format: $IDF_TARGET"
}

install_apt_packages() {
  if [[ "$INSTALL_SYSTEM_PACKAGES" != "1" ]]; then
    log "Skipping OS package install (set INSTALL_SYSTEM_PACKAGES=1 to enable)"
    return
  fi

  command -v apt-get >/dev/null 2>&1 || die "apt-get not available, cannot install packages"

  log "Installing OS packages required for ESP-IDF + this repo"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  apt-get install -y --no-install-recommends \
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
  "$IDF_PATH/install.sh" "$IDF_TARGET"
}

validate_idf_py() {
  [[ -f "$IDF_PATH/export.sh" ]] || die "expected export script missing: $IDF_PATH/export.sh"

  log "Validating idf.py availability"
  # shellcheck disable=SC1090
  source "$IDF_PATH/export.sh" >/dev/null

  require_cmd idf.py
  idf.py --version
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
  log "Starting Codex Cloud bootstrap for meatreader-esp"
  validate_inputs
  install_apt_packages
  install_esp_idf
  validate_idf_py
  install_ui_deps
  summary
}

main "$@"
