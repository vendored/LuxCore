#!/bin/bash

#automated script build for macos 10.13 should compile and patch with dependencies in root/macos
# set python and bison env *
eval "$(pyenv init -)"
pyenv shell 3.7.4
export PATH="/usr/local/opt/bison/bin:/usr/local/bin:$PATH"

# build luxcore
rm -rf build

mkdir build

cd build

cmake ..

make -j8

cd ..

# bundle things 
rm -rf release_OSX 

mkdir release_OSX

###luxcoreui

./scripts/install_target_luxcoreui.sh

echo "LuxCoreUi installed"

###luxcoreconsole

./scripts/install_target_luxcoreconsole.sh

echo "LuxCoreConsole installed"

###pyluxcore

./scripts/install_target_pyluxcore.sh

echo "PyLuxCore installed"

###denoise

./scripts/install_target_denoise.sh

echo "denoise installed"

#tar -czf LuxCoreRender.tar.gz ./release_OSX
echo "Creating DMG ..."

hdiutil create LuxCoreRender2.3alpha0.dmg -volname "LuxCoreRender2.3alpha0" -fs HFS+ -srcfolder release_OSX/

echo "DMG created !"