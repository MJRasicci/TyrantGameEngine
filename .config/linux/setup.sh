#!/usr/bin/env bash

set -uo pipefail

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
${BOLD}Tyrant Game Engine Linux Dependency Setup${RESET}

${BOLD}Usage:${RESET}  $(basename "$0") [options]

${BOLD}Options:${RESET}
  -h, --help        Show this help and exit
  -v, --verbose     Show command output (default is quiet)
  -y, --yes         Non-interactive: auto-confirm all prompts and pass -y to the package manager
  -r, --required    Only install required packages (skip docs & test/benchmark deps)

${BOLD}Required dependencies:${RESET}
  * GCC / Clang toolchain (via distro meta-packages)
  * ninja
  * cmake

${BOLD}Optional dependencies:${RESET}
  * Doxygen
  * Graphviz
  * GTest
  * Google Benchmark

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

PM=""             # Package manager binary name (apt-get, dnf, pacman, ...)
PM_NAME=""        # Human-readable name for logs
PM_SUDO="sudo"    # Prefix for privileged invocations
PM_Y_FLAG=""      # Package-manager specific "assume yes" flag
PM_QUIET_FLAGS="" # Flags that silence installer output
PM_INSTALL_CMD="" # Actual install subcommand (e.g. "apt-get install")
PM_UPDATE_CMD=""  # Optional metadata refresh command (e.g. "apt-get update")

command_exists() { command -v "$1" >/dev/null 2>&1; }

set_pm_config() {
  # Configure per-package-manager execution details.
  case "$PM" in
    apt-get)
      PM_NAME="APT (Debian/Ubuntu)"
      PM_INSTALL_CMD="apt-get install"
      PM_Y_FLAG="-y"
      PM_QUIET_FLAGS="-qq"
      PM_UPDATE_CMD="apt-get update"
      ;;
    dnf)
      PM_NAME="DNF (RHEL/Fedora)"
      PM_INSTALL_CMD="dnf install"
      PM_Y_FLAG="-y"
      PM_QUIET_FLAGS="-q"
      ;;
    pacman)
      PM_NAME="pacman (Arch/Manjaro)"
      PM_INSTALL_CMD="pacman -S --needed"
      PM_Y_FLAG="--noconfirm"
      PM_QUIET_FLAGS="--noprogressbar --quiet"
      if [[ "$ASSUME_YES" -eq 1 ]]; then
        PM_UPDATE_CMD="pacman -Syu --noconfirm"
      else
        PM_UPDATE_CMD="pacman -Syu"
      fi
      ;;
    zypper)
      PM_NAME="Zypper (openSUSE)"
      PM_INSTALL_CMD="zypper install"
      PM_Y_FLAG="-y"
      PM_QUIET_FLAGS="-q"
      PM_UPDATE_CMD="zypper refresh"
      ;;
    apk)
      PM_NAME="apk (Alpine)"
      PM_INSTALL_CMD="apk add"
      PM_Y_FLAG="--no-interactive"
      PM_QUIET_FLAGS="-q"
      PM_UPDATE_CMD="apk update"
      PM_SUDO="sudo"
      ;;
    emerge)
      PM_NAME="Portage (Gentoo)"
      PM_INSTALL_CMD="emerge --quiet --noreplace"
      PM_Y_FLAG="--ask=n"
      PM_QUIET_FLAGS="--quiet"
      ;;
    *)
      err "Unsupported package manager: $PM"
      exit 3
      ;;
  esac
}

detect_platform() {
  if [[ "$OS" == "Darwin" ]]; then
    err "This script targets Linux environments. Use .config/macos/setup.sh on macOS."
    exit 3
  fi

  if [[ -f /etc/os-release ]]; then
    . /etc/os-release
  fi

  for candidate in apt-get dnf pacman zypper apk emerge; do
    if command_exists "$candidate"; then
      PM="$candidate"
      set_pm_config
      return
    fi
  done

  err "Unsupported platform. This script supports apt, dnf, pacman, zypper, apk, and emerge."
  note "Tip: extend this script by adding new package metadata in the maps and updating detect_platform()."
  exit 3
}

detect_platform

# Drop sudo prefix if already running as root or sudo is unavailable (e.g. minimal containers).
if [[ ${EUID:-$(id -u)} -eq 0 ]] || ! command_exists sudo; then
  PM_SUDO=""
fi

# --------------------------- Dependency maps ---------------------------
declare -A REQ_MAP OPT_MAP

REQ_MAP[apt-get]="build-essential ninja-build cmake"
OPT_MAP[apt-get]="doxygen graphviz libgtest-dev libbenchmark-dev"

REQ_MAP[dnf]="gcc gcc-c++ ninja-build cmake"
OPT_MAP[dnf]="doxygen graphviz gtest-devel google-benchmark-devel"

REQ_MAP[pacman]="base-devel ninja cmake"
OPT_MAP[pacman]="doxygen graphviz gtest benchmark"

REQ_MAP[zypper]="gcc gcc-c++ ninja cmake"
OPT_MAP[zypper]="doxygen graphviz gtest benchmark-devel"

REQ_MAP[apk]="build-base ninja cmake"
OPT_MAP[apk]="doxygen graphviz gtest benchmark"

REQ_MAP[emerge]="sys-devel/gcc dev-build/ninja dev-build/cmake"
OPT_MAP[emerge]="app-text/doxygen media-gfx/graphviz dev-cpp/gtest dev-cpp/benchmark"

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

gentoo_pkg_db="/var/db/pkg"

gentoo_is_installed() {
  # Gentoo records installed packages in /var/db/pkg/<category>/<name>-<version> directories.
  local atom="$1"
  [[ "$atom" == */* ]] || return 1
  local category="${atom%/*}"
  local name="${atom##*/}"
  compgen -G "${gentoo_pkg_db}/${category}/${name}-*" >/dev/null 2>&1
}

gentoo_pkg_version() {
  local atom="$1"
  [[ "$atom" == */* ]] || return 0
  local category="${atom%/*}"
  local name="${atom##*/}"
  local match
  match="$(compgen -G "${gentoo_pkg_db}/${category}/${name}-*" | head -n1)"
  [[ -n "$match" ]] || return 0
  match="$(basename "$match")"
  echo "${match#${name}-}"
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
    build-essential|gcc|gcc-c++|sys-devel/gcc)
      if command_exists gcc; then tool_version gcc; fi
      ;;
    ninja-build|ninja|dev-build/ninja)
      tool_version ninja
      ;;
    cmake|dev-build/cmake)
      tool_version cmake
      ;;
    doxygen|app-text/doxygen)
      tool_version doxygen
      ;;
    graphviz|media-gfx/graphviz)
      # 'dot -V' prints to stderr; tool_version handles it
      tool_version dot
      ;;
    libgtest-dev|gtest-devel|googletest|gtest|dev-cpp/gtest)
      echo "(headers/libs present if package installed)"
      ;;
    libbenchmark-dev|google-benchmark|google-benchmark-devel|benchmark|dev-cpp/benchmark)
      echo "(libs present if package installed)"
      ;;
    base-devel)
      echo "(meta-package for GNU toolchain)"
      ;;
    build-base)
      echo "(meta-package for Alpine build toolchain)"
      ;;
    *)
      ;;
  esac
}

# --------------------------- Plan selection ---------------------------
declare -a REQ_PKGS OPT_PKGS ALL_PKGS

populate_package_plan() {
  local req_list opt_list
  req_list="${REQ_MAP[$PM]:-}"
  opt_list="${OPT_MAP[$PM]:-}"

  if [[ -n "$req_list" ]]; then
    read -r -a REQ_PKGS <<< "$req_list"
  else
    REQ_PKGS=()
  fi

  if [[ -n "$opt_list" ]]; then
    read -r -a OPT_PKGS <<< "$opt_list"
  else
    OPT_PKGS=()
  fi
}

populate_package_plan

if [[ "$REQUIRED_ONLY" -eq 1 ]]; then
  ALL_PKGS=("${REQ_PKGS[@]}")
else
  ALL_PKGS=("${REQ_PKGS[@]}" "${OPT_PKGS[@]}")
fi

# --------------------------- Package audit ---------------------------
PKG_STATUS=()   # lines for table
MISSING_PKGS=() # package ids to install

is_pkg_installed() {
  local pkg="$1"
  case "$PM" in
    apt-get)
      dpkg -s "$pkg" >/dev/null 2>&1
      ;;
    dnf|zypper)
      rpm -q "$pkg" >/dev/null 2>&1
      ;;
    pacman)
      pacman -Qi "$pkg" >/dev/null 2>&1
      ;;
    apk)
      apk info -e "$pkg" >/dev/null 2>&1
      ;;
    emerge)
      gentoo_is_installed "$pkg"
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
    dnf|zypper)
      pkg_version_dnf "$pkg"
      ;;
    pacman)
      pacman -Qi "$pkg" 2>/dev/null | awk -F': ' '/^Version/ {print $2}'
      ;;
    apk)
      apk info "$pkg" 2>/dev/null | awk 'NR==1 {print $1}' | sed 's/^[^0-9]*-//'
      ;;
    emerge)
      gentoo_pkg_version "$pkg"
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

  local yflag="" qflags=""
  [[ "$ASSUME_YES" -eq 1 ]] && yflag="$PM_Y_FLAG"

  # Run package metadata refresh if required
  if [[ -n "$PM_UPDATE_CMD" && "$VERBOSE" -eq 1 ]]; then
    $PM_SUDO $PM_UPDATE_CMD
  elif [[ -n "$PM_UPDATE_CMD" ]]; then
    $PM_SUDO $PM_UPDATE_CMD >/dev/null 2>&1 || true
  fi

  # Gentoo-specific preflight to ensure proper feature flags for graphviz dependencies
  if [[ "$PM" == "emerge" ]]; then
    # Ensure Portage is synced enough to resolve
    eselect news read new || true
    emerge-webrsync || true
    emaint sync -a || true

    # Satisfy graphviz -> gd USE requirements
    mkdir -p /etc/portage/package.use
    # Keep it idempotent: append only if not present
    if ! grep -q '^media-libs/gd ' /etc/portage/package.use/tge 2>/dev/null; then
      echo 'media-libs/gd fontconfig truetype' >> /etc/portage/package.use/tge
    fi
  fi

  if [[ "$VERBOSE" -eq 1 ]]; then
    $PM_SUDO $PM_INSTALL_CMD $yflag "${MISSING_PKGS[@]}"
  else
    qflags="$PM_QUIET_FLAGS"
    $PM_SUDO $PM_INSTALL_CMD $yflag $qflags "${MISSING_PKGS[@]}" >/dev/null 2>&1
  fi

  if [[ $? -eq 0 ]]; then
    ok "Installation completed."
  else
    err "Installation failed. Re-run with -v for details."
    return 1
  fi
}

# --------------------------- Main ---------------------------
title "Tyrant Game Engine Linux Dependency Setup"
info "Platform: ${OS}; Package Manager: ${PM_NAME}"

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
