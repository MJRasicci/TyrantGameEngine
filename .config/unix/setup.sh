#!/usr/bin/env bash
# setup.sh — Cross-platform dependency bootstrap (Debian/Ubuntu, RHEL/Fedora, macOS)
# Usage: ./setup.sh [-h|--help] [-v|--verbose] [-y|--yes] [-r|--required]
#
# Required deps:
#   Debian/Ubuntu: build-essential, ninja-build, cmake
#   RHEL/Fedora:   gcc, gcc-c++, ninja-build, cmake
#   macOS (brew):  xcode-select (CLT), cmake, ninja
#
# Optional deps:
#   Debian/Ubuntu: doxygen, graphviz, libgtest-dev, libbenchmark-dev
#   RHEL/Fedora:   doxygen, graphviz, gtest-devel, google-benchmark-devel
#   macOS (brew):  doxygen, graphviz, googletest, google-benchmark
#
# Behavior:
#   - Detects platform & package manager
#   - Prints a well-formatted audit with versions (where discoverable)
#   - Prompts (or auto-confirms with -y) to install missing
#   - Quiet by default; -v shows command output
#   - Extensible: add more platforms by implementing the *_pkgs maps

set -uo pipefail

# --------------------------- Styling ---------------------------
if [[ -t 1 ]]; then
  BOLD="$(tput bold)"; DIM="$(tput dim)"; RESET="$(tput sgr0)"
  GREEN="$(tput setaf 2)"; YELLOW="$(tput setaf 3)"; RED="$(tput setaf 1)"; CYAN="$(tput setaf 6)"; BLUE="$(tput setaf 4)"
else
  BOLD=""; DIM=""; RESET=""; GREEN=""; YELLOW=""; RED=""; CYAN=""; BLUE=""
fi

bar() { printf "%s\n" "${DIM}────────────────────────────────────────────────────────────────────────────${RESET}"; }
title() { bar; printf "%s%s%s\n" "$BOLD" "$1" "$RESET"; bar; }
note() { printf "%s%s%s\n" "$DIM" "$1" "$RESET"; }
ok()   { printf "%s✓%s %s\n" "$GREEN" "$RESET" "$1"; }
warn() { printf "%s•%s %s\n" "$YELLOW" "$RESET" "$1"; }
err()  { printf "%s✗%s %s\n" "$RED" "$RESET" "$1"; }
info() { printf "%sℹ%s %s\n" "$CYAN" "$RESET" "$1"; }

# --------------------------- CLI parsing ---------------------------
VERBOSE=0
ASSUME_YES=0
REQUIRED_ONLY=0

print_help() {
  cat <<EOF
${BOLD}Tyrant Game Engine Dependency Setup${RESET}

${BOLD}Usage:${RESET}  $(basename "$0") [options]

${BOLD}Options:${RESET}
  -h, --help        Show this help and exit
  -v, --verbose     Show command output (default is quiet)
  -y, --yes         Non-interactive: auto-confirm all prompts and pass -y to the package manager
  -r, --required    Only install required packages (skip docs & test/benchmark deps)

${BOLD}Required dependencies:${RESET}
  Debian/Ubuntu: build-essential, ninja-build, cmake
  RHEL/Fedora:   gcc, gcc-c++, ninja-build, cmake
  macOS (brew):  Xcode CLT, cmake, ninja

${BOLD}Optional dependencies:${RESET}
  doxygen, graphviz, gtest/googletest, google-benchmark

${BOLD}Examples:${RESET}
  $(basename "$0") -y              # Install everything non-interactively
  $(basename "$0") -r              # Only required toolchain & build tools
  $(basename "$0") -v -y           # Non-interactive and verbose
EOF
}

# parse args (short + long)
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

PM=""             # apt-get | dnf | brew (macOS uses brew + Xcode CLT)
PM_NAME=""        # Human-readable
PM_SUDO="sudo"    # will prefix installs
PM_Y_FLAG="-y"
PM_QUIET_FLAGS=""
PM_INSTALL_CMD=""

command_exists() { command -v "$1" >/dev/null 2>&1; }

if [[ "$OS" == "Darwin" ]]; then
  PM="brew"; PM_NAME="Homebrew"
  PM_SUDO=""         # brew doesn't need sudo for standard prefix
  PM_Y_FLAG=""       # not applicable
  PM_QUIET_FLAGS=""  # brew is reasonably chatty; we’ll control via redirection
  PM_INSTALL_CMD="brew install"
elif [[ -f /etc/os-release ]]; then
  . /etc/os-release
  if command_exists apt-get; then
    PM="apt-get"; PM_NAME="APT (Debian/Ubuntu)"
    PM_QUIET_FLAGS="-qq"
    PM_INSTALL_CMD="apt-get install"
  elif command_exists dnf; then
    PM="dnf"; PM_NAME="DNF (RHEL/Fedora)"
    PM_QUIET_FLAGS="-q"
    PM_INSTALL_CMD="dnf install"
  fi
fi

if [[ -z "$PM" ]]; then
  err "Unsupported platform. This script supports Debian/Ubuntu (apt), RHEL/Fedora (dnf), and macOS (brew)."
  note "Tip: extend this script by adding new package maps and a detection case in the platform section."
  exit 3
fi

# --------------------------- Dependency maps ---------------------------
# Logical dependency names -> platform package IDs and version probes

# On Linux, compilers:
#   - Debian: build-essential meta-package; version via dpkg -s
#   - RHEL:   gcc/gcc-c++; versions via rpm -q
# On macOS: compiler via Xcode CLT

# Logical -> (pkg ids)
REQ_DEB=( "build-essential" "ninja-build" "cmake" )
OPT_DEB=( "doxygen" "graphviz" "libgtest-dev" "libbenchmark-dev" )

REQ_RPM=( "gcc" "gcc-c++" "ninja-build" "cmake" )
OPT_RPM=( "doxygen" "graphviz" "gtest-devel" "google-benchmark-devel" )

REQ_BREW=( "cmake" "ninja" )
OPT_BREW=( "doxygen" "graphviz" "googletest" "google-benchmark" )

# --------------------------- Version detection helpers ---------------------------
pkg_version_apt() {
  # dpkg -s outputs 'Version: x.y.z'
  local pkg="$1"
  dpkg -s "$pkg" 2>/dev/null | awk -F': ' '/^Version:/ {print $2}'
}

pkg_version_dnf() {
  # rpm -q returns NAME-VERSION-RELEASE... or "not installed"
  local pkg="$1"
  rpm -q --qf '%{VERSION}-%{RELEASE}\n' "$pkg" 2>/dev/null || true
}

brew_version() {
  local formula="$1"
  # e.g. "cmake 3.30.2" possibly with multiple versions
  brew list --versions "$formula" 2>/dev/null | awk '{print $2}'
}

tool_version() {
  # Extract the first version-looking token from a tool's --version output.
  # arg1: cmd, arg2 (optional): custom grep regex
  local cmd="$1"; shift
  local rx="${1:-[0-9]+([.][0-9]+)+}"
  command -v "$cmd" >/dev/null 2>&1 || return 1

  # Try a few common flags; stop at the first that prints something.
  local out
  for flag in "--version" "-version" "-v" "-V"; do
    out="$("$cmd" "$flag" 2>&1 | LC_ALL=C tr -d '\r' || true)"
    [[ -n "$out" ]] && break
  done
  [[ -z "$out" ]] && return 1

  # Pull the first version token (works across GNU/BSD tools).
  echo "$out" | grep -Eo "$rx" | head -n1
}

# Map logical to executable probes (for nicer version lines)
# Some packages don't expose an executable (gtest, benchmark libs)
probe_version_for_logical() {
  local logical="$1"
  case "$logical" in
    build-essential|gcc|gcc-c++)
      if command_exists gcc; then tool_version gcc; fi
      ;;
    ninja-build|ninja)
      tool_version ninja
      ;;
    cmake)
      tool_version cmake
      ;;
    doxygen)
      tool_version doxygen
      ;;
    graphviz)
      # 'dot -V' prints to stderr; tool_version handles it
      tool_version dot
      ;;
    libgtest-dev|gtest-devel|googletest)
      echo "(headers/libs present if package installed)"
      ;;
    libbenchmark-dev|google-benchmark|google-benchmark-devel)
      echo "(libs present if package installed)"
      ;;
    xcode-clt)
      # Presence implies Clang tools; show clang version if available
      if command_exists clang; then tool_version clang; else echo "installed"; fi
      ;;
    homebrew)
      brew --version 2>/dev/null | head -n1 | awk '{print $2}'
      ;;
    *)
      ;;
  esac
}

# --------------------------- Plan selection ---------------------------
declare -a REQ_PKGS OPT_PKGS ALL_PKGS

if [[ "$OS" == "Darwin" ]]; then
  REQ_PKGS=("${REQ_BREW[@]}")
  OPT_PKGS=("${OPT_BREW[@]}")
else
  if [[ "$PM" == "apt-get" ]]; then
    REQ_PKGS=("${REQ_DEB[@]}")
    OPT_PKGS=("${OPT_DEB[@]}")
  else
    REQ_PKGS=("${REQ_RPM[@]}")
    OPT_PKGS=("${OPT_RPM[@]}")
  fi
fi

if [[ "$REQUIRED_ONLY" -eq 1 ]]; then
  ALL_PKGS=("${REQ_PKGS[@]}")
else
  ALL_PKGS=("${REQ_PKGS[@]}" "${OPT_PKGS[@]}")
fi

# --------------------------- macOS prerequisites ---------------------------
ensure_macos_prereqs() {
  local need_brew=0 need_xcode=0
  if [[ "$OS" != "Darwin" ]]; then return 0; fi

  # Xcode Command Line Tools
  if xcode-select -p >/dev/null 2>&1; then
    ok "Xcode Command Line Tools: present"
  else
    need_xcode=1
    warn "Xcode Command Line Tools: missing"
    if [[ "$ASSUME_YES" -eq 1 ]]; then
      info "Attempting to trigger Xcode CLT install (GUI may prompt)..."
      xcode-select --install || true
      info "After CLT completes, re-run this script if compilers aren't available yet."
    else
      read -r -p "Install Xcode Command Line Tools now? [Y/n] " ans
      ans="${ans:-Y}"
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        xcode-select --install || true
        info "Installation initiated (GUI). Re-run if needed after completion."
      else
        warn "Skipping Xcode CLT install; required compilers may be missing."
      fi
    fi
  fi

  # Homebrew
  if command_exists brew; then
    ok "Homebrew: present ($(probe_version_for_logical homebrew))"
  else
    need_brew=1
    warn "Homebrew: missing"
    if [[ "$ASSUME_YES" -eq 1 ]]; then
      /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    else
      read -r -p "Install Homebrew now? [Y/n] " ans
      ans="${ans:-Y}"
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
      else
        err "Homebrew is required to install dependencies on macOS."
      fi
    fi
    # Try to add brew to PATH for this session if needed
    if [[ -x /opt/homebrew/bin/brew ]]; then
      eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [[ -x /usr/local/bin/brew ]]; then
      eval "$(/usr/local/bin/brew shellenv)"
    fi
  fi
}

# --------------------------- Package audit ---------------------------
PKG_STATUS=()   # lines for table
MISSING_PKGS=() # package ids to install

is_pkg_installed() {
  local pkg="$1"
  case "$PM" in
    apt-get)
      dpkg -s "$pkg" >/dev/null 2>&1
      ;;
    dnf)
      rpm -q "$pkg" >/dev/null 2>&1
      ;;
    brew)
      # Homebrew first
      if brew list --versions "$pkg" >/dev/null 2>&1; then
        return 0
      fi

      # Fallbacks for non-brew installs on macOS
      case "$pkg" in
        cmake)
          command -v cmake >/dev/null 2>&1
          return $?
          ;;
        ninja)
          command -v ninja >/dev/null 2>&1
          return $?
          ;;
        doxygen)
          command -v doxygen >/dev/null 2>&1
          return $?
          ;;
        graphviz)
          # Graphviz exposes 'dot'
          command -v dot >/dev/null 2>&1
          return $?
          ;;
        googletest|googletest@*)
          # Heuristic: presence via pkg-config (MacPorts/source installs often expose this)
          if command -v pkg-config >/dev/null 2>&1; then
            pkg-config --exists gtest 2>/dev/null && return 0
            pkg-config --exists gtest_main 2>/dev/null && return 0
          fi
          return 1
          ;;
        google-benchmark|google-benchmark@*|benchmark)
          if command -v pkg-config >/dev/null 2>&1; then
            pkg-config --exists benchmark 2>/dev/null && return 0
          fi
          return 1
          ;;
        *)
          return 1
          ;;
      esac
      ;;
    *)
      return 1
      ;;
  esac
}

pkg_version() {
  local pkg="$1"
  case "$PM" in
    apt-get)
      pkg_version_apt "$pkg"
      ;;
    dnf)
      pkg_version_dnf "$pkg"
      ;;
    brew)
      # Prefer Homebrew metadata; if not installed via brew, fall back to tool probe and tag it.
      local v
      v="$(brew_version "$pkg")"
      if [[ -n "$v" ]]; then
        echo "$v"
      else
        v="$(probe_version_for_logical "$pkg")"
        [[ -n "$v" ]] && echo "$v [non-brew]"
      fi
      ;;
    *)
      echo ""
      ;;
  esac
}

audit_packages() {
  # RESET STATE so re-audits don't reuse old results
  MISSING_PKGS=()
  PKG_STATUS=()

  title "Dependency Audit (${PM_NAME})"
  printf "%-26s | %-10s | %s\n" "Package" "Status" "Version/Details"
  bar

  # macOS special "logical" checks for nicer versions
  if [[ "$OS" == "Darwin" ]]; then
    # Show Xcode CLT status
    if xcode-select -p >/dev/null 2>&1; then
      printf "%-26s | %s%-10s%s | %s\n" "xcode-commandline-tools" "$GREEN" "present" "$RESET" "$(probe_version_for_logical xcode-clt)"
    else
      printf "%-26s | %s%-10s%s | %s\n" "xcode-commandline-tools" "$RED" "missing" "$RESET" "-"
    fi
    # Show Homebrew line
    if command_exists brew; then
      printf "%-26s | %s%-10s%s | %s\n" "homebrew" "$GREEN" "present" "$RESET" "$(probe_version_for_logical homebrew)"
    else
      printf "%-26s | %s%-10s%s | %s\n" "homebrew" "$RED" "missing" "$RESET" "-"
    fi
    bar
  fi

  for pkg in "${ALL_PKGS[@]}"; do
    local status="missing" color="$RED" ver="-"
    if is_pkg_installed "$pkg"; then
      status="present"; color="$GREEN"
      # Try to show either package version or a tool probe
      ver="$(pkg_version "$pkg")"
      if [[ -z "$ver" || "$ver" == "(none)" ]]; then
        ver="$(probe_version_for_logical "$pkg")"
        [[ -z "$ver" ]] && ver="-"
      fi
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
    read -r -p "Proceed with installation? [Y/n] " ans
    ans="${ans:-Y}"
    if ! [[ "$ans" =~ ^[Yy]$ ]]; then
      warn "User declined installation."
      return 1
    fi
  fi

  title "Installing Missing Packages"

  if [[ "$OS" == "Darwin" ]]; then
    # brew update recommended before install
    if [[ "$VERBOSE" -eq 1 ]]; then
      brew update
      brew install "${MISSING_PKGS[@]}"
    else
      brew update >/dev/null 2>&1
      brew install "${MISSING_PKGS[@]}" >/dev/null 2>&1
    fi
  else
    local yflag="" qflags=""
    [[ "$ASSUME_YES" -eq 1 ]] && yflag="$PM_Y_FLAG"
    if [[ "$VERBOSE" -eq 1 ]]; then
      $PM_SUDO $PM_INSTALL_CMD $yflag "${MISSING_PKGS[@]}"
    else
      # quiet mode
      qflags="$PM_QUIET_FLAGS"
      $PM_SUDO $PM_INSTALL_CMD $yflag $qflags "${MISSING_PKGS[@]}" >/dev/null 2>&1
    fi
  fi

  if [[ $? -eq 0 ]]; then
    ok "Installation completed."
  else
    err "Installation failed. Re-run with -v for details."
    return 1
  fi
}

# --------------------------- Main ---------------------------
title "Tyrant Game Engine Dependency Setup"
info "Platform: ${OS}; Package Manager: ${PM_NAME}"

if [[ "$OS" == "Darwin" ]]; then
  ensure_macos_prereqs
fi

audit_packages
if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
  install_missing || {
    err "Some dependencies remain missing."
    exit 4
  }
  # Re-audit after install
  echo
  audit_packages
fi

ok "Setup finished."
