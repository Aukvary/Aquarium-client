#!/usr/bin/env sh
set -eu

VERSION="${AQUARIUM_VERSION:-1.0.0}"
REPO_URL="https://github.com/Aukvary/Aquarium-client"
RAW_BASE="https://raw.githubusercontent.com/Aukvary/Aquarium-client/main"

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

tty_exec() {
  if [ -r /dev/tty ]; then
    "$@" < /dev/tty
  else
    "$@"
  fi
}

install_arch() {
  if [ "$(id -u)" -eq 0 ]; then
    echo "Error: do not run this script as root on Arch Linux (makepkg cannot run as root)." >&2
    exit 1
  fi

  current_shell="${SHELL:-sh}"
  echo "Detected Arch-based system (${current_shell##*/}). Installing via PKGBUILD..."
  curl -fsSL "${RAW_BASE}/packaging/arch/PKGBUILD" -o "${TMP_DIR}/PKGBUILD"
  (
    cd "$TMP_DIR"
    tty_exec makepkg -si
  )
}

build_from_source() {
  echo "Building Aquarium-client v${VERSION} from source..."
  git clone --depth 1 --branch "v${VERSION}" "${REPO_URL}.git" "${TMP_DIR}/Aquarium-client"

  if command -v ninja >/dev/null 2>&1; then
    cmake -B "${TMP_DIR}/build" -S "${TMP_DIR}/Aquarium-client" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_DISABLE_FIND_PACKAGE_ftxui=TRUE \
      -DBUILD_TESTING=OFF
  else
    cmake -B "${TMP_DIR}/build" -S "${TMP_DIR}/Aquarium-client" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_DISABLE_FIND_PACKAGE_ftxui=TRUE \
      -DBUILD_TESTING=OFF
  fi

  cmake --build "${TMP_DIR}/build"
  tty_exec sudo cmake --install "${TMP_DIR}/build" --prefix /usr/local
}

if command -v pacman >/dev/null 2>&1 && command -v makepkg >/dev/null 2>&1; then
  install_arch
elif command -v dnf >/dev/null 2>&1; then
  echo "Detected Fedora/DNF-based system. Installing dependencies..."
  tty_exec sudo dnf install -y gcc-c++ cmake ninja-build git protobuf-devel protobuf-compiler grpc-devel grpc-plugins
  build_from_source
elif command -v apt-get >/dev/null 2>&1; then
  echo "Detected Debian/Ubuntu-based system. Installing dependencies..."
  tty_exec sudo apt-get update
  tty_exec sudo apt-get install -y build-essential cmake ninja-build git libprotobuf-dev protobuf-compiler libgrpc++-dev protobuf-compiler-grpc
  build_from_source
elif command -v cmake >/dev/null 2>&1 && command -v git >/dev/null 2>&1; then
  echo "Unknown package manager; trying generic CMake build with installed dependencies..."
  build_from_source
else
  echo "Unsupported distribution or missing build tools (cmake, git, C++20, protobuf, grpc)." >&2
  exit 1
fi

echo "Done! Run: aquarium-client --help"
