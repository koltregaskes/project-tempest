#!/usr/bin/env bash
# Start the Substation 9 rules session. This opens no window and needs no retail assets.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${root}/build/tempest-session"

probe_compiler() {
    local compiler="$1"
    local probe_dir
    probe_dir="$(mktemp -d)"
    if printf 'int main(){return 0;}\n' | "${compiler}" -x c++ -o "${probe_dir}/probe" - >"${probe_dir}/log" 2>&1; then
        rm -rf "${probe_dir}"
        return 0
    fi
    rm -rf "${probe_dir}"
    return 1
}

choose_compiler() {
    if [[ -n "${CXX:-}" ]]; then
        if probe_compiler "${CXX}"; then
            printf '%s\n' "${CXX}"
            return
        fi
        echo "CXX=${CXX} cannot link a C++ program." >&2
        exit 1
    fi
    local candidate
    for candidate in g++ c++ clang++; do
        if command -v "${candidate}" >/dev/null 2>&1 && probe_compiler "${candidate}"; then
            printf '%s\n' "${candidate}"
            return
        fi
    done
    echo "No working C++20 compiler found. Install g++ or set CXX." >&2
    exit 1
}

compiler="$(choose_compiler)"
if [[ -f "${build}/CMakeCache.txt" ]]; then
    cached="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "${build}/CMakeCache.txt" | head -1)"
    resolved="$(command -v "${compiler}" || true)"
    if [[ -n "${cached}" && "${cached}" != "${compiler}" && "${cached}" != "${resolved}" ]]; then
        rm -rf "${build}"
    fi
fi

cmake -S "${root}/ProjectTempest/session" -B "${build}" -D "CMAKE_CXX_COMPILER=${compiler}"
cmake --build "${build}" --parallel
exec "${build}/project_tempest_session" "$@"
