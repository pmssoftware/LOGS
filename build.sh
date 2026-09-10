#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")" && pwd)"
sqlite_dir="$root_dir/work/vendor/sqlite-amalgamation-3530400"
build_dir="$root_dir/work/build"
out_dir="$root_dir/outputs"

target=x86_64-w64-mingw32
local_tool_bin="$root_dir/work/toolchain/opt/llvm-mingw/bin"

if command -v "$target-gcc" >/dev/null &&
   command -v "$target-g++" >/dev/null &&
   command -v "$target-windres" >/dev/null; then
  cc="$(command -v "$target-gcc")"
  cxx="$(command -v "$target-g++")"
  windres="$(command -v "$target-windres")"
elif command -v "$target-clang" >/dev/null &&
     command -v "$target-clang++" >/dev/null &&
     command -v "$target-windres" >/dev/null; then
  cc="$(command -v "$target-clang")"
  cxx="$(command -v "$target-clang++")"
  windres="$(command -v "$target-windres")"
elif [[ -x "$local_tool_bin/$target-clang" &&
        -x "$local_tool_bin/$target-clang++" &&
        -x "$local_tool_bin/$target-windres" ]]; then
  cc="$local_tool_bin/$target-clang"
  cxx="$local_tool_bin/$target-clang++"
  windres="$local_tool_bin/$target-windres"
else
  echo "Kein geeigneter 64-Bit-Windows-Cross-Compiler gefunden." >&2
  echo "Installieren Sie MinGW-w64 oder legen Sie LLVM-MinGW unter work/toolchain/opt/llvm-mingw ab." >&2
  exit 1
fi

for required in \
  "$sqlite_dir/sqlite3.c" \
  "$sqlite_dir/sqlite3.h" \
  "$root_dir/src/logs.ico" \
  "$root_dir/src/qr-logo.png"; do
  [[ -f "$required" ]] || { echo "Fehlende Build-Abhängigkeit: $required" >&2; exit 1; }
done

mkdir -p "$build_dir" "$out_dir"

"$windres" "$root_dir/src/resource.rc" -I "$root_dir/src" -O coff -o "$build_dir/resource.o"

"$cc" -O2 -DNDEBUG -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 \
  -c "$sqlite_dir/sqlite3.c" -o "$build_dir/sqlite3.o"

"$cxx" -std=c++17 -O2 -DNDEBUG -municode -mwindows -static \
  -I "$sqlite_dir" -I "$root_dir/src" \
  "$root_dir/src/main.cpp" "$root_dir/src/qrcodegen.cpp" "$build_dir/sqlite3.o" "$build_dir/resource.o" \
  -o "$out_dir/LogS.exe" -lcomctl32 -lcomdlg32 -lwinspool -lshell32 -lole32 -luuid -luxtheme -lgdiplus -lwinpthread

echo "Created $out_dir/LogS.exe"
