# Private Xwayland: never replaces /usr/bin/Xwayland or either game launcher.
# Keep makepkg output separate from the checked-in src/ directory.
[[ ${BUILDDIR:-$startdir} != "$startdir" ]] || BUILDDIR="$startdir/.local/build"
[[ ${SRCDEST:-$startdir} != "$startdir" ]] || SRCDEST="$startdir/src"
pkgname=runelite-hidpi
pkgver=0.1.0
pkgrel=26
pkgdesc='Experimental whole-window RuneLite HiDPI bridge for KDE Wayland'
arch=('x86_64')
url='https://github.com/ollieatkinson/runelite-hidpi'
license=('MIT' 'BSD-2-Clause')
_xwaylandver=26.0.99.903
depends=('bash' 'jq' 'util-linux' 'qt6-tools' 'kdialog' 'xorg-xdpyinfo'
         'glibc' 'libx11' 'libxdamage' 'libdecor' 'libdrm' 'libepoxy' 'libglvnd' 'libtirpc'
         'libxau' 'libxcvt' 'libxfont2' 'libxshmfence' 'mesa' 'nettle' 'pixman'
         'wayland>=1.26.0' 'xorg-server-common')
makedepends=('meson' 'ninja' 'gawk' 'libxkbfile' 'xorgproto' 'xorg-font-util'
             'xtrans>=1.6.0' 'wayland-protocols>=1.49')
source=("https://www.x.org/releases/individual/xserver/xwayland-$_xwaylandver.tar.xz"
        "xwayland.patch::file://${startdir}/src/xwayland.patch" "nested-wm.c::file://${startdir}/src/nested-wm.c"
        "runelite-hidpi.sh::file://${startdir}/src/runelite-hidpi.sh" "runelite.png::file://${startdir}/src/runelite.png"
        "net.runelite.RuneLite.HiDPI.desktop::file://${startdir}/src/net.runelite.RuneLite.HiDPI.desktop"
        "RuneLite-LICENSE::file://${startdir}/src/RuneLite-LICENSE"
        "bicubic.patch::file://${startdir}/src/bicubic.patch"
        "runelite-bicubic.h::file://${startdir}/src/runelite-bicubic.h"
        "runelite-bicubic-gpu.h::file://${startdir}/src/runelite-bicubic-gpu.h"
        "runelite-bicubic.c::file://${startdir}/src/runelite-bicubic.c"
        "runelite-bicubic-gpu.c::file://${startdir}/src/runelite-bicubic-gpu.c"
        "bicubic.vert.glsl::file://${startdir}/src/bicubic.vert.glsl"
        "bicubic.frag.glsl::file://${startdir}/src/bicubic.frag.glsl"
        "embed-shader.awk::file://${startdir}/src/embed-shader.awk"
        "host-window.js::file://${startdir}/src/host-window.js"
        "runelite-present.c::file://${startdir}/src/runelite-present.c"
        "runelite-present.h::file://${startdir}/src/runelite-present.h")
sha256sums=(
    'fa07af573ff6e6f4b4a656c958b16ee77cafeb28189a8c7a4f384284165b020b' # xwayland-26.0.99.903.tar.xz
    '574de9dd000c3539887ede1548a207e41f8c25d4bed67d0072c45fd6b880eee1' # xwayland.patch
    'db181fd0ae49d8a7a5d840c8b8fac04942043881cb0f09d53a7e4ad29b5746b7' # nested-wm.c
    'a9908eb06414c1a6f07e0ed2226af77a034b90e6d3e53015e89a01893c38a173' # runelite-hidpi.sh
    '81cb6ce7d8c4b9154e9840ab9d2938d0e6234f227049f004cacf90724f95cc11' # runelite.png
    'ae06ee6dbbacfdd1298271c14bb630b434a34e3e434bc46d05b0301b318adac8' # net.runelite.RuneLite.HiDPI.desktop
    'dd35829a586785621651857262168bd91374a84669592d2db28520648b55a519' # RuneLite-LICENSE
    '7d73a0b5439deb7d222790ddd1de1f5698c076d4077ea98c9ed91277c265c838' # bicubic.patch
    'e5ff562a16d3a84da9175cc7861fa73077c35f9d8fd3e758392988d7b1bf3e89' # runelite-bicubic.h
    '22764998fd222ede1d77f4ee9791b70857c3ce13ced2d75c4ae59b56629ec395' # runelite-bicubic-gpu.h
    '57cdbce0fa0acd8eabbde6f1d44a1bd5bd11a45fa7fc1bb2f13bb696de6b0c14' # runelite-bicubic.c
    'ccce9e6944088b34c2719061f7b8c69234d762a91877f3d87183a57c212a132e' # runelite-bicubic-gpu.c
    '9d6d6349ec5c472c7df45bcfaeaa3262869b1f127561c0500b66bdcec8d5696c' # bicubic.vert.glsl
    '7f0fe158159645993b567ebeb4fcede859b6af50cf135cd478050c18490d27cd' # bicubic.frag.glsl
    '86236c6e9bc215e1a14fd5c66e3872c395f3bd03f177d0d2019131e607c1e62f' # embed-shader.awk
    '6ab1531937c2be892eb6b78cf48d7fcc066161c0cc0007b01dba97a90b541d32' # host-window.js
    '83e7b82f45f563cfb6c0b5e0b0bdf73a79b3957da5cca6c1cbb617973fe16bb0' # runelite-present.c
    '5de1b4c32e1e23bbc1d4181b5c875ec4f1ed5ccf54b00ab19ad4a88f47c03e9e' # runelite-present.h
)

prepare() {
    cd "$srcdir/xwayland-$_xwaylandver"
    patch -Np1 -i "$srcdir/xwayland.patch"
    patch -Np1 -i "$srcdir/bicubic.patch"
    cp "$srcdir"/runelite-bicubic{,-gpu}.{c,h} "$srcdir"/runelite-present.{c,h} \
        "$srcdir"/bicubic.{vert,frag}.glsl \
        "$srcdir/embed-shader.awk" hw/xwayland/
}

build() {
    meson setup "$srcdir/build" "$srcdir/xwayland-$_xwaylandver" \
        --prefix=/usr --buildtype=release -Dxwayland_ei=false \
        -Dglamor=true -Dglx=true -Ddocs=false -Dxvfb=false
    ninja -C "$srcdir/build" -j "${RUNELITE_HIDPI_BUILD_JOBS:-4}"
    cc $CFLAGS "$srcdir/nested-wm.c" $LDFLAGS -o "$srcdir/nested-wm" -lX11 -lXdamage
}


package() {
    install -Dm755 "$srcdir/build/hw/xwayland/Xwayland" "$pkgdir/usr/lib/runelite-hidpi/Xwayland"
    install -Dm644 "$srcdir/host-window.js" "$pkgdir/usr/lib/runelite-hidpi/host-window.js"
    install -Dm755 "$srcdir/nested-wm" "$pkgdir/usr/lib/runelite-hidpi/nested-wm"
    install -Dm755 "$srcdir/runelite-hidpi.sh" "$pkgdir/usr/bin/runelite-hidpi"
    install -Dm644 "$srcdir/net.runelite.RuneLite.HiDPI.desktop" "$pkgdir/usr/share/applications/net.runelite.RuneLite.HiDPI.desktop"
    install -Dm644 "$srcdir/net.runelite.RuneLite.HiDPI.desktop" "$pkgdir/usr/lib/runelite-hidpi/net.runelite.RuneLite.HiDPI.desktop"
    install -Dm644 "$srcdir/runelite.png" "$pkgdir/usr/share/icons/hicolor/128x128/apps/runelite-hidpi.png"
    install -Dm644 "$srcdir/xwayland-$_xwaylandver/COPYING" "$pkgdir/usr/share/licenses/$pkgname/Xwayland-COPYING"
    install -Dm644 "$srcdir/RuneLite-LICENSE" "$pkgdir/usr/share/licenses/$pkgname/RuneLite-LICENSE"
}
