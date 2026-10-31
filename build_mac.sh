#!/bin/bash
# Vox Rehab — build VST3 + AU + Standalone on macOS, install them, and make a DMG installer.
# Usage:  cd VoxRehab && ./build_mac.sh
set -euo pipefail
cd "$(dirname "$0")"

VERSION="1.0.0"
NAME="Vox Rehab"

echo "==> Checking tools"
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode command line tools missing. Run:  xcode-select --install   then re-run this script."; exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then brew install cmake
  else echo "cmake missing. Install Homebrew (https://brew.sh) then: brew install cmake"; exit 1; fi
fi

if [ ! -d JUCE/modules ]; then
  echo "==> Fetching JUCE 8.0.4"
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
fi

echo "==> Configuring (universal: Apple Silicon + Intel)"
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

echo "==> Building (takes a few minutes)"
cmake --build build-mac --config Release -j "$(sysctl -n hw.ncpu)" \
      --target VoxRehab_VST3 VoxRehab_AU VoxRehab_Standalone

ART="build-mac/VoxRehab_artefacts/Release"
VST3="$ART/VST3/$NAME.vst3"
AU="$ART/AU/$NAME.component"
APP="$ART/Standalone/$NAME.app"

echo "==> Ad-hoc signing (so macOS will load it on this Mac)"
for b in "$VST3" "$AU" "$APP"; do codesign --force --deep -s - "$b"; done

echo "==> Installing for this user"
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/"$NAME.vst3" ~/Library/Audio/Plug-Ins/Components/"$NAME.component"
cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$AU" ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true   # make Logic/GarageBand rescan AUs

echo "==> Building installer package + DMG"
rm -rf dist pkgroot dmgsrc && mkdir -p dist dmgsrc \
  "pkgroot/Library/Audio/Plug-Ins/VST3" "pkgroot/Library/Audio/Plug-Ins/Components" "pkgroot/Applications"
cp -R "$VST3" "pkgroot/Library/Audio/Plug-Ins/VST3/"
cp -R "$AU"   "pkgroot/Library/Audio/Plug-Ins/Components/"
cp -R "$APP"  "pkgroot/Applications/"
pkgbuild --analyze --root pkgroot dist/components.plist >/dev/null
# keep bundles where we put them (don't let the installer "relocate" them)
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" dist/components.plist >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" dist/components.plist >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" dist/components.plist
  i=$((i+1))
done
pkgbuild --root pkgroot --component-plist dist/components.plist \
         --identifier com.joecaple.voxrehab --version "$VERSION" --install-location / \
         "dmgsrc/Install $NAME.pkg"
cp README.md "dmgsrc/Read Me.md"
hdiutil create -volname "$NAME $VERSION" -srcfolder dmgsrc -ov -format UDZO "dist/VoxRehab-$VERSION.dmg" >/dev/null
rm -rf pkgroot dmgsrc dist/components.plist

echo ""
echo "Done."
echo "  Installed:  ~/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
echo "              ~/Library/Audio/Plug-Ins/Components/$NAME.component"
echo "  Installer:  $(pwd)/dist/VoxRehab-$VERSION.dmg   (for other Macs)"
echo "  Standalone: $APP"
echo "Rescan plug-ins in your DAW. Logic: Settings > Plug-in Manager > Reset & Rescan Selection."
