#!/bin/bash
# CDDA mobile fork: desktop test tools, SDL3 from source, Android SDK and NDK.
# Runs from the directory that holds the checkout.  Never fails the session start.
START=$PWD
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq || true
apt-get install -y -qq ccache gettext astyle glslang-tools xvfb xdotool imagemagick x11-utils \
  unzip pkg-config libfreetype-dev libharfbuzz-dev libpng-dev libjpeg-dev zlib1g-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libxtst-dev libxkbcommon-dev libgl1-mesa-dev libegl1-mesa-dev python3-pil
GH=https://github.com/libsdl-org
sdl() { # repo version extra-cmake-flags
  [ -f /usr/local/lib/cmake/$1/$1Config.cmake ] && return 0
  mkdir -p /tmp/sdlsrc && cd /tmp/sdlsrc
  curl -fsSL --retry 4 -o $1.tar.gz $GH/${1/SDL3/SDL}/releases/download/release-$2/$1-$2.tar.gz
  tar xf $1.tar.gz
  cmake -S $1-$2 -B b-$1 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local $3
  cmake --build b-$1 -j4 && cmake --install b-$1 > /dev/null
  cd / && rm -rf /tmp/sdlsrc
}
sdl SDL3 3.4.10 "-DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_UNIX_CONSOLE_BUILD=OFF"
sdl SDL3_image 3.4.4 "-DSDLIMAGE_AVIF=OFF -DSDLIMAGE_JXL=OFF -DSDLIMAGE_TIF=OFF -DSDLIMAGE_WEBP=OFF"
sdl SDL3_ttf 3.2.2 "-DSDLTTF_VENDORED=OFF"
ldconfig
SDK=/opt/android-sdk
if [ ! -d $SDK/ndk/28.1.13356709 ]; then
  mkdir -p $SDK/cmdline-tools && cd /tmp
  curl -fsSL --retry 4 -o clt.zip \
    https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip
  unzip -q -o clt.zip -d $SDK/cmdline-tools && rm clt.zip
  rm -rf $SDK/cmdline-tools/latest && mv $SDK/cmdline-tools/cmdline-tools $SDK/cmdline-tools/latest
  yes | $SDK/cmdline-tools/latest/bin/sdkmanager --licenses > /dev/null || true
  $SDK/cmdline-tools/latest/bin/sdkmanager "platform-tools" "platforms;android-35" \
    "build-tools;35.0.0" "ndk;28.1.13356709" "cmake;3.22.1" > /dev/null
fi
cd "$START"
if [ -d Cataclysm-DDA-mobile/android ]; then
  echo "sdk.dir=$SDK" > Cataclysm-DDA-mobile/android/local.properties
fi
# picture tilesets, for the desktop game and bundled into the APK
[ -d Cataclysm-DDA-mobile/gfx ] && bash Cataclysm-DDA-mobile/tools/mobile_ui/fetch_tilesets.sh || true
echo "setup done"
exit 0
