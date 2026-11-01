#!/usr/bin/env bash
# Builds the macOS installer package from a Release build.
#
#   packaging/macos/build-pkg.sh <version> [artefacts dir] [output dir]
#
# The package offers two choices: the VST3 plugin (/Library/Audio/Plug-Ins/VST3)
# and the Standalone app (/Applications). It is not signed with a Developer ID;
# the bundles get an ad-hoc signature so they run on Apple silicon.
set -euo pipefail

version="${1:?usage: build-pkg.sh <version> [artefacts dir] [output dir]}"
artefacts="${2:-build/Floorplan_artefacts/Release}"
out="${3:-dist}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/vst3" "$work/app" "$work/resources" "$out"

cp -R "$artefacts/VST3/Floorplan.vst3" "$work/vst3/"
cp -R "$artefacts/Standalone/Floorplan.app" "$work/app/"
codesign --force --deep --sign - "$work/vst3/Floorplan.vst3"
codesign --force --deep --sign - "$work/app/Floorplan.app"

# Component packages. Bundles are marked non-relocatable, otherwise Installer
# "upgrades" any other copy of the bundle it finds (a build folder, say)
# instead of installing to the intended location.
component() # <name> <identifier> <install location>
{
    local plist="$work/$1.plist"
    pkgbuild --analyze --root "$work/$1" "$plist"
    local i=0
    while plutil -extract "$i" xml1 -o /dev/null "$plist" 2>/dev/null; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
        i=$((i + 1))
    done
    pkgbuild --root "$work/$1" --component-plist "$plist" --identifier "$2" \
             --version "$version" --install-location "$3" "$work/Floorplan-$1.pkg"
}
component vst3 io.github.kuzuw.floorplan.vst3 "/Library/Audio/Plug-Ins/VST3"
component app  io.github.kuzuw.floorplan.app  "/Applications"

cp "$root/LICENSE" "$work/resources/LICENSE.txt"
sed "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"

productbuild --distribution "$work/distribution.xml" --package-path "$work" \
             --resources "$work/resources" "$out/Floorplan-$version-macOS.pkg"
echo "Wrote $out/Floorplan-$version-macOS.pkg"
