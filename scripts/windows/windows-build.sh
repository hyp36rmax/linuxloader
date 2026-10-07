#!/usr/bin/env bash
set -euo pipefail

make win32 -j"$(nproc)"

loader="build-win32/linuxloader.exe"
test -s "$loader"
file "$loader" | grep -Eq 'PE32 executable .*Intel 80386'
grep -aFq 'AER_DRIVEBOARD_RAW_V1' "$loader"

mkdir win-release
cp "$loader" win-release/linuxloader.exe
#cp libs/win32/SDL3.dll win-release/SDL3.dll
cp -r libs/win32/ll-deps win-release/
cp /usr/lib/gcc/i686-w64-mingw32/13-win32/libgcc_s_dw2-1.dll win-release/ll-deps/

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
zip -r ../linuxloader-win32.zip .
cd ..

unzip -p linuxloader-win32.zip linuxloader.exe > packaged-linuxloader.exe
cmp "$loader" packaged-linuxloader.exe
rm packaged-linuxloader.exe
