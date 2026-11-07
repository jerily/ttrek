# Runtime preflight for install/update. Keep this independent of project state.
missing=
for tool in bash cmake make curl git patch pkg-config perl tar gzip; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
# Honor explicit compiler commands, including wrappers such as 'ccache gcc'.
if [ -n "${CC:-}" ]; then
    compiler=${CC%% *}
    command -v "$compiler" >/dev/null 2>&1 || missing="$missing CC=$compiler"
elif ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    missing="$missing C-compiler(gcc/clang)"
fi
if [ -n "${CXX:-}" ]; then
    compiler=${CXX%% *}
    command -v "$compiler" >/dev/null 2>&1 || missing="$missing CXX=$compiler"
elif ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    missing="$missing C++-compiler(g++/clang++)"
fi
if [ -n "$missing" ]; then
    printf 'ttrek: missing external build dependencies:%s\n' "$missing" >&2
    printf '%s\n' \
        'Install them before retrying this command.' \
        'Ubuntu/Debian:' \
        '  sudo apt update' \
        '  sudo apt install build-essential cmake bash curl git patch pkg-config perl tar gzip ca-certificates' \
        'build-essential supplies GCC, G++ and Make.' \
        'For a newer CMake, use: sudo snap install cmake --classic' \
        'On other systems, install the equivalent tools with your system package manager.' \
        'Ensure the installed commands are on PATH (including /snap/bin when using snap).' >&2
    exit 1
fi
