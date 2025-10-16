#!/usr/bin/env zsh

set -u
set -o pipefail

# --------------------------- Styling ---------------------------
if [[ -t 1 ]] && [[ -n "${TERM:-}" && "${TERM}" != "dumb" ]] && command -v tput >/dev/null 2>&1 && [[ -z "${NO_COLOR:-}" ]]; then
  BOLD="$(tput bold)"; DIM="$(tput dim)"; RESET="$(tput sgr0)"
  GREEN="$(tput setaf 2)"; YELLOW="$(tput setaf 3)"; RED="$(tput setaf 1)"; CYAN="$(tput setaf 6)"; BLUE="$(tput setaf 4)"
else
  BOLD=""; DIM=""; RESET=""; GREEN=""; YELLOW=""; RED=""; CYAN=""; BLUE=""
fi

bar()   { printf "%s\n" "${DIM}────────────────────────────────────────────────────────────────────────────${RESET}"; }
title() { bar; printf "%s%s%s\n" "$BOLD" "$1" "$RESET"; bar; }
note()  { printf "%s%s%s\n" "$DIM" "$1" "$RESET"; }
ok()    { printf "%s✓%s %s\n" "$GREEN" "$RESET" "$1"; }
warn()  { printf "%s•%s %s\n" "$YELLOW" "$RESET" "$1"; }
err()   { printf "%s✗%s %s\n" "$RED" "$RESET" "$1"; }
info()  { printf "%sℹ%s %s\n" "$CYAN" "$RESET" "$1"; }

# --------------------------- CLI parsing ---------------------------
VERBOSE=0
ASSUME_YES=0
REQUIRED_ONLY=0

print_help() {
  cat <<EOF
${BOLD}Tyrant Game Engine macOS Dependency Setup${RESET}

${BOLD}Usage:${RESET}  $(basename "$0") [options]

${BOLD}Options:${RESET}
  -h, --help        Show this help and exit
  -v, --verbose     Show command output (default is quiet)
  -y, --yes         Non-interactive: auto-confirm all prompts
  -r, --required    Only install required packages (skip docs & test/benchmark deps)

${BOLD}Required dependencies:${RESET}
  * Xcode Command Line Tools
  * Homebrew
  * ninja
  * cmake

${BOLD}Optional dependencies:${RESET}
  * Doxygen
  * Graphviz
  * GTest
  * Google Benchmark

${BOLD}Examples:${RESET}
  $(basename "$0") -y              # Install everything non-interactively
  $(basename "$0") -r              # Only required build tools
  $(basename "$0") -v -y           # Non-interactive and verbose
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) print_help; exit 0 ;;
    -v|--verbose) VERBOSE=1; shift ;;
    -y|--yes) ASSUME_YES=1; shift ;;
    -r|--required) REQUIRED_ONLY=1; shift ;;
    *) err "Unknown option: $1"; print_help; exit 2 ;;
  esac
done

# --------------------------- Environment detection ---------------------------
OS="$(uname -s || true)"
if [[ "$OS" != "Darwin" ]]; then
  err "This script targets macOS environments. Use .config/linux/setup.sh on Linux."
  exit 3
fi

command_exists() { command -v "$1" >/dev/null 2>&1 }

# --------------------------- Dependency definitions ---------------------------
typeset -ga REQ_PKGS=(cmake ninja)
typeset -ga OPT_PKGS=(doxygen graphviz googletest google-benchmark)
typeset -ga ALL_PKGS

typeset -ga MISSING_PKGS

tool_version() {
  local cmd="$1"; shift
  local rx="${1:-[0-9]+([.][0-9]+)+}"
  command_exists "$cmd" || return 1
  local out=""
  local flag
  for flag in --version -version -v -V; do
    out="$($cmd $flag 2>&1 | LC_ALL=C tr -d '\r' || true)"
    [[ -n "$out" ]] && break
  done
  [[ -z "$out" ]] && return 1
  echo "$out" | grep -Eo "$rx" | head -n1
}

probe_version_for_logical() {
  local logical="$1"
  case "$logical" in
    ninja)
      tool_version ninja
      ;;
    cmake)
      tool_version cmake
      ;;
    doxygen)
      tool_version doxygen
      ;;
    graphviz)
      tool_version dot
      ;;
    googletest)
      echo "(headers/libs present if package installed)"
      ;;
    google-benchmark)
      echo "(libs present if package installed)"
      ;;
    *)
      ;;
  esac
}

populate_package_plan() {
  if [[ "$REQUIRED_ONLY" -eq 1 ]]; then
    ALL_PKGS=(${REQ_PKGS[@]})
  else
    ALL_PKGS=(${REQ_PKGS[@]} ${OPT_PKGS[@]})
  fi
}

populate_package_plan

# --------------------------- macOS prerequisites ---------------------------
ensure_xcode_clt() {
  if xcode-select -p >/dev/null 2>&1; then
    ok "Xcode Command Line Tools: present"
    return 0
  fi

  warn "Xcode Command Line Tools: missing"
  if [[ "$ASSUME_YES" -eq 1 ]]; then
    info "Attempting to trigger Xcode Command Line Tools installation..."
    xcode-select --install || true
    note "After installation completes, re-run this script if compilers are still unavailable."
  else
    printf "Install Xcode Command Line Tools now? [Y/n] "
    local ans
    read -r ans
    ans="${ans:-Y}"
    if [[ "$ans" =~ ^[Yy]$ ]]; then
      xcode-select --install || true
      info "Installation initiated (GUI). Re-run after completion if necessary."
    else
      warn "Skipping Xcode Command Line Tools installation; compilers may be missing."
    fi
  fi
}

ensure_homebrew() {
  if command_exists brew; then
    ok "Homebrew: present"
    return 0
  fi

  warn "Homebrew: missing"
  if [[ "$ASSUME_YES" -eq 1 ]]; then
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  else
    printf "Install Homebrew now? [Y/n] "
    local ans
    read -r ans
    ans="${ans:-Y}"
    if [[ "$ans" =~ ^[Yy]$ ]]; then
      /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    else
      warn "Skipping Homebrew installation; dependency installation will fail."
    fi
  fi

  if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ -x /usr/local/bin/brew ]]; then
    eval "$(/usr/local/bin/brew shellenv)"
  fi

  if ! command_exists brew; then
    err "Homebrew is required to install dependencies on macOS."
    exit 3
  fi
}

ensure_prereqs() {
  ensure_xcode_clt
  ensure_homebrew
}

# --------------------------- Package audit ---------------------------
brew_pkg_installed() {
  brew list --versions "$1" >/dev/null 2>&1
}

pkg_version() {
  brew list --versions "$1" 2>/dev/null | awk '{print $2}'
}

audit_packages() {
  MISSING_PKGS=()
  title "Dependency Audit (Homebrew)"
  printf "%-26s | %-10s | %s\n" "Package" "Status" "Version/Details"
  bar

  if xcode-select -p >/dev/null 2>&1; then
    printf "%-26s | %s%-10s%s | %s\n" "xcode-commandline-tools" "$GREEN" "present" "$RESET" "$(tool_version clang || echo installed)"
  else
    printf "%-26s | %s%-10s%s | %s\n" "xcode-commandline-tools" "$RED" "missing" "$RESET" "-"
  fi
  if command_exists brew; then
    local brew_ver
    brew_ver="$(brew --version 2>/dev/null | head -n1 | awk '{print $2}')"
    printf "%-26s | %s%-10s%s | %s\n" "homebrew" "$GREEN" "present" "$RESET" "${brew_ver:-unknown}"
  else
    printf "%-26s | %s%-10s%s | %s\n" "homebrew" "$RED" "missing" "$RESET" "-"
  fi
  bar

  local pkg status color ver
  for pkg in ${ALL_PKGS[@]}; do
    status="missing"; color="$RED"; ver="-"
    if brew_pkg_installed "$pkg"; then
      status="present"; color="$GREEN"
      ver="$(pkg_version "$pkg")"
      [[ -z "$ver" ]] && ver="$(probe_version_for_logical "$pkg")"
      [[ -z "$ver" ]] && ver="-"
    else
      MISSING_PKGS+=("$pkg")
    fi
    printf "%-26s | %s%-10s%s | %s\n" "$pkg" "$color" "$status" "$RESET" "$ver"
  done
  bar

  if [[ ${#MISSING_PKGS[@]} -eq 0 ]]; then
    ok "All requested dependencies are present."
  else
    warn "Missing packages: ${MISSING_PKGS[*]}"
  fi
}

# --------------------------- Installer ---------------------------
install_missing() {
  [[ ${#MISSING_PKGS[@]} -eq 0 ]] && return 0

  if [[ "$ASSUME_YES" -eq 0 ]]; then
    printf "%sMissing packages detected:%s %s\n" "$YELLOW" "$RESET" "${MISSING_PKGS[*]}"
    printf "Proceed with installation? [Y/n] "
    local ans
    read -r ans
    ans="${ans:-Y}"
    if ! [[ "$ans" =~ ^[Yy]$ ]]; then
      warn "User declined installation."
      return 1
    fi
  fi

  title "Installing Missing Packages"
  local quiet_flag=""
  [[ "$VERBOSE" -eq 0 ]] && quiet_flag="--quiet"

  if [[ "$VERBOSE" -eq 1 ]]; then
    brew update
  else
    brew update >/dev/null 2>&1 || true
  fi

  if [[ "$VERBOSE" -eq 1 ]]; then
    brew install ${MISSING_PKGS[@]}
  else
    brew install $quiet_flag ${MISSING_PKGS[@]} >/dev/null 2>&1
  fi

  if [[ $? -eq 0 ]]; then
    ok "Installation completed."
  else
    err "Installation failed. Re-run with -v for details."
    return 1
  fi
}

# --------------------------- Main ---------------------------
title "Tyrant Game Engine macOS Dependency Setup"
info "Platform: ${OS}; Package Manager: Homebrew"

ensure_prereqs
populate_package_plan
audit_packages
if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
  install_missing || {
    err "Some dependencies remain missing."
    exit 4
  }
  echo
  audit_packages
fi

ok "Setup finished."
