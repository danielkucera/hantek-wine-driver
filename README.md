# Wine driver for Hantek USB oscilloscope

## Installation

I haven't been able to find a way to build an "out-of-tree" driver so you need to download full sources for your wine version

- check your wine version:
```
$ wine --version
wine-6.0.3 (Ubuntu 6.0.3~repack-1)
```
- clone corresponding wine version, apply patch and install driver:
```
git clone --branch wine-6.0.3 --depth=1 https://github.com/wine-mirror/wine

cd wine

git clone https://github.com/danielkucera/hantek-wine-driver dlls/hantek.sys

patch -p1 < dlls/hantek.sys/0001-add-hantek.sys.patch

cd dlls/hantek.sys/

./build.sh
```
- try to run `wine Scope.exe`

## Debugging

- to see debug info, export folling variable and run scope again:
```
export WINEDEBUG=trace+hantek
```
