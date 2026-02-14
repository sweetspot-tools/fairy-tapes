#!/usr/bin/env bash
#
# Fairy Tapes — Build script for the plugin suite.
# Usage:
#   ./build.sh              Build all plugins (tests run first; build aborts if tests fail).
#   ./build.sh <plugin>     Build only the given plugin (e.g. the-stripper); tests run first.
#   ./build.sh --configure  Only run CMake configure, do not build.
#
# Plugin folder name is converted to CMake target by replacing '-' with '_'
# (e.g. the-stripper -> the_stripper).

set -e

SUITE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SUITE_ROOT}/build"

# Folder name (e.g. the-stripper) -> CMake target (e.g. the_stripper)
folder_to_target() {
  echo "$1" | tr '-' '_'
}

do_configure() {
  echo "Configuring CMake in ${BUILD_DIR}..."
  mkdir -p "${BUILD_DIR}"
  (cd "${BUILD_DIR}" && cmake ..)
}

# Build test targets for the given plugin (or all). Fails if tests do not pass.
do_tests() {
  local target="$1"
  echo "Building and running tests..."
  if [[ -n "${target}" ]]; then
    local test_target="${target}_tests"
    (cd "${BUILD_DIR}" && cmake --build . --target "${test_target}")
  else
    (cd "${BUILD_DIR}" && cmake --build . --target the_stripper_tests)
  fi
  (cd "${BUILD_DIR}" && ctest --output-on-failure) || { echo "Tests failed. Aborting build."; exit 1; }
}

do_build() {
  local target="$1"
  do_configure
  do_tests "${target}"
  if [[ -n "${target}" ]]; then
    echo "Building plugin target: ${target}"
    (cd "${BUILD_DIR}" && cmake --build . --target "${target}")
  else
    echo "Building all plugins..."
    (cd "${BUILD_DIR}" && cmake --build .)
  fi
}

# Parse arguments
if [[ "$1" == "--configure" ]]; then
  mkdir -p "${BUILD_DIR}"
  (cd "${BUILD_DIR}" && cmake ..)
  exit 0
fi

if [[ -n "$1" ]]; then
  PLUGIN_TARGET=$(folder_to_target "$1")
  do_build "${PLUGIN_TARGET}"
else
  do_build ""
fi
