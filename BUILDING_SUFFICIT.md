# Building Sufficit Phone (Ubuntu 26.04)

Notes specific to this fork's build environment, on top of the upstream `Linphone/README.md` instructions.

## Clone

```sh
git clone --recursive git@github.com:sufficit/sufficit-phone.git
```

If `gitlab.linphone.org` is unreachable from your network (some ISPs/networks block it while GitHub/GitLab.com work fine), tunnel git traffic through a host that can reach it:

```sh
ssh -D 1080 -N -f root@<some-host-with-access>
git config --global http.https://gitlab.linphone.org/.proxy socks5h://127.0.0.1:1080
```

## System dependencies (apt)

```sh
sudo apt-get install -y \
  qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-multimedia-dev \
  qt6-tools-dev qt6-tools-dev-tools qt6-networkauth-dev qt6-shadertools-dev \
  qt6-l10n-tools qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  build-essential ninja-build libssl-dev libxml2-dev libsqlite3-dev \
  bison flex yasm nasm python3-pip pkg-config \
  doxygen graphviz meson \
  libudev-dev libasound2-dev libpulse-dev libv4l-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavdevice-dev \
  libx11-dev libxext-dev libxrandr-dev libgl1-mesa-dev libgtk-3-dev \
  libglew-dev libxcb-cursor0 \
  libldap-dev \
  zlib1g-dev libbz2-dev libreadline-dev libncurses-dev libspeex-dev libspeexdsp-dev \
  autoconf automake libtool intltool

pip3 install --break-system-packages pystache cython pdoc
```

## Configure + build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined"
cmake --build build --parallel $(nproc)
```

`-Wl,--allow-shlib-undefined` is required: Ubuntu's `libcurl-gnutls.so.4` references OpenLDAP symbols that resolve fine at runtime (it declares `NEEDED libldap.so.2`/`liblber.so.2`), but `ld` fails at link time without this flag since it checks a shared lib's own undefined symbols by default.

## Packaging (AppImage)

Packaging is gated by `ENABLE_APP_PACKAGING` (default OFF):

```sh
cmake -S . -B build -DENABLE_APP_PACKAGING=ON
cmake --build build --parallel $(nproc)
```

`linuxdeploy-plugin-qt` expects `qmake` at `<Qt6Core_DIR>/../../../bin/qmake` (matching the official Qt installer's layout). Debian/Ubuntu's system Qt6 package puts binaries in `/usr/bin/qmake6` instead, so create the path it expects:

```sh
sudo mkdir -p /usr/lib/bin
sudo ln -sf /usr/bin/qmake6 /usr/lib/bin/qmake
```

Then:

```sh
cmake --install build
```

Result: `build/OUTPUT/Packages/Sufficit Phone-<version>-x86_64.AppImage`
