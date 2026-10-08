#!/usr/bin/env bash
set -euo pipefail

make win32 -j"$(nproc)"

loader="build-win32/linuxloader.exe"
test -s "$loader"
file "$loader" | grep -Eq 'PE32 executable .*Intel 80386'
grep -aFq 'AER_DRIVEBOARD_RAW_V1' "$loader"
grep -aFq 'AER_DRIVEBOARD_ACTIVATION_V1' "$loader"
grep -aFq 'AER_NATIVE_ACTIVATION_V1' "$loader"

short_sha="${GITHUB_SHA:-local}"
short_sha="${short_sha:0:7}"
archive="LinuxLoader-DEV4-AER01K-WIN32-${short_sha}.zip"

mkdir win-release
cp "$loader" win-release/linuxloader.exe
#cp libs/win32/SDL3.dll win-release/SDL3.dll
cp -r libs/win32/ll-deps win-release/
cp /usr/lib/gcc/i686-w64-mingw32/13-win32/libgcc_s_dw2-1.dll win-release/ll-deps/
cat > win-release/BUILD_INFO.txt <<EOF
Full commit SHA: ${GITHUB_SHA:-UNKNOWN}
CI run ID: ${GITHUB_RUN_ID:-LOCAL}
Platform: Windows i686 MinGW
Research milestone: AER-01K
Recorder schema: AER_DRIVEBOARD_RAW_V1
Diagnostic version: AER_DRIVEBOARD_ACTIVATION_V1
Native activation schema: AER_NATIVE_ACTIVATION_V1
Game revision target: DVP-0015A
EOF

required_dependencies=(
    libstdc++.so.6
    libgcc_s_dw2-1.dll
    zlib1.dll
    openal32.dll
    zink/libgallium_wgl.dll
    zink/opengl32.dll
)
for dependency in "${required_dependencies[@]}"; do
    test -s "win-release/ll-deps/$dependency"
done

cd win-release
zip -r "../$archive" .
cd ..

unzip -p "$archive" linuxloader.exe > packaged-linuxloader.exe
cmp "$loader" packaged-linuxloader.exe
rm packaged-linuxloader.exe
