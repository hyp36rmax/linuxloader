#!/usr/bin/env bash
set -euo pipefail

make win32 -j"$(nproc)"

loader="build-win32/linuxloader.exe"
test -s "$loader"
file "$loader" | grep -Eq 'PE32 executable .*Intel 80386'
grep -aFq 'AER_DRIVEBOARD_RAW_V1' "$loader"
grep -aFq 'AER_DRIVEBOARD_ACTIVATION_V1' "$loader"
grep -aFq 'AER_NATIVE_ACTIVATION_V1' "$loader"
grep -aFq 'AER_VIRTUAL_DRIVEBOARD_STATUS_V1' "$loader"

# Keep the CI artifact, ZIP filename and embedded metadata tied to the exact source commit.
commit_sha="${GITHUB_SHA:-$(git rev-parse HEAD)}"
if [[ ! "$commit_sha" =~ ^[[:xdigit:]]{40}$ ]]; then
    echo "Invalid commit SHA for DEV 5 package: $commit_sha" >&2
    exit 1
fi
short_commit="${commit_sha:0:7}"
archive="LinuxLoader-DEV5-AER02H-WIN32-${short_commit}.zip"

mkdir win-release
cp "$loader" win-release/linuxloader.exe
#cp libs/win32/SDL3.dll win-release/SDL3.dll
cp -r libs/win32/ll-deps win-release/
cp /usr/lib/gcc/i686-w64-mingw32/13-win32/libgcc_s_dw2-1.dll win-release/ll-deps/
cp research/dev5/Run-DEV5-Native-FFB.cmd win-release/
cp research/dev5/Finalize-DEV5-Capture.ps1 win-release/
cp research/dev5/DEV5-virtual-driveboard.ini win-release/
cp research/dev5/DEV5-README.txt win-release/
cat > win-release/BUILD_INFO.txt <<EOF
Full commit SHA: ${commit_sha}
CI run ID: ${GITHUB_RUN_ID:-LOCAL}
Platform: Windows i686 MinGW
Research milestone: AER-02H DEV 5
Recorder schema: AER_DRIVEBOARD_RAW_V1
Diagnostic version: AER_DRIVEBOARD_ACTIVATION_V1
Native activation schema: AER_NATIVE_ACTIVATION_V1
Virtual board status schema: AER_VIRTUAL_DRIVEBOARD_STATUS_V1
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

required_package_files=(
    linuxloader.exe
    Run-DEV5-Native-FFB.cmd
    Finalize-DEV5-Capture.ps1
    DEV5-virtual-driveboard.ini
    DEV5-README.txt
    BUILD_INFO.txt
)
for package_file in "${required_package_files[@]}"; do
    test -s "win-release/$package_file"
done

cd win-release
zip -r "../$archive" .
cd ..

unzip -p "$archive" linuxloader.exe > packaged-linuxloader.exe
cmp "$loader" packaged-linuxloader.exe
rm packaged-linuxloader.exe
unzip -p "$archive" BUILD_INFO.txt | grep -Fq "Full commit SHA: ${commit_sha}"
for package_file in "${required_package_files[@]}"; do
    unzip -Z1 "$archive" | grep -Fxq "$package_file"
done
