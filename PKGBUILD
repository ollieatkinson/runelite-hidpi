# Private Xwayland: never replaces /usr/bin/Xwayland or either game launcher.
# Keep makepkg output separate from the checked-in src/ directory.
[[ ${BUILDDIR:-$startdir} != "$startdir" ]] || BUILDDIR="$startdir/.local/build"
[[ ${SRCDEST:-$startdir} != "$startdir" ]] || SRCDEST="$startdir/src"
pkgname=runelite-hidpi
pkgver=0.1.0
pkgrel=2
pkgdesc='Experimental whole-window RuneLite HiDPI bridge for KDE Wayland'
arch=('x86_64')
url='https://github.com/ollieatkinson/runelite-hidpi'
license=('MIT' 'BSD-2-Clause')
_xwaylandver=24.1.14
depends=('bash' 'jq' 'util-linux' 'qt6-tools' 'kdialog' 'xorg-xdpyinfo'
         'glibc' 'libx11' 'libdecor' 'libdrm' 'libepoxy' 'libglvnd' 'libtirpc'
         'libxau' 'libxcvt' 'libxfont2' 'libxshmfence' 'mesa' 'nettle' 'pixman'
         'wayland' 'xorg-server-common')
makedepends=('meson' 'ninja' 'libxkbfile' 'xorgproto' 'xorg-font-util'
             'xtrans>=1.6.0' 'wayland-protocols>=1.49')
source=("https://www.x.org/releases/individual/xserver/xwayland-$_xwaylandver.tar.xz"
        "xwayland.patch::file://${startdir}/src/xwayland.patch" "nested-wm.c::file://${startdir}/src/nested-wm.c"
        "runelite-hidpi.sh::file://${startdir}/src/runelite-hidpi.sh" "runelite.png::file://${startdir}/src/runelite.png"
        "net.runelite.RuneLite.HiDPI.desktop::file://${startdir}/src/net.runelite.RuneLite.HiDPI.desktop"
        "RuneLite-LICENSE::file://${startdir}/src/RuneLite-LICENSE")
sha256sums=('4eb6b98d678299a4b96138d886345b294130695760345ef84596225a8224078a')
# Local sources are checked against the hashes below, updated when source changes.

prepare() {
    cd "$srcdir/xwayland-$_xwaylandver"
    patch -Np1 -i "$srcdir/xwayland.patch"
}

build() {
    meson setup "$srcdir/build" "$srcdir/xwayland-$_xwaylandver" \
        --prefix=/usr --buildtype=release -Dxwayland_ei=false \
        -Dglamor=true -Dglx=true -Ddocs=false -Dxvfb=false
    ninja -C "$srcdir/build" -j "${RUNELITE_HIDPI_BUILD_JOBS:-4}"
    cc $CFLAGS "$srcdir/nested-wm.c" $LDFLAGS -o "$srcdir/nested-wm" -lX11
}


package() {
    install -Dm755 "$srcdir/build/hw/xwayland/Xwayland" "$pkgdir/usr/lib/runelite-hidpi/Xwayland"
    install -Dm755 "$srcdir/nested-wm" "$pkgdir/usr/lib/runelite-hidpi/nested-wm"
    install -Dm755 "$srcdir/runelite-hidpi.sh" "$pkgdir/usr/bin/runelite-hidpi"
    install -Dm644 "$srcdir/net.runelite.RuneLite.HiDPI.desktop" "$pkgdir/usr/share/applications/net.runelite.RuneLite.HiDPI.desktop"
    install -Dm644 "$srcdir/net.runelite.RuneLite.HiDPI.desktop" "$pkgdir/usr/lib/runelite-hidpi/net.runelite.RuneLite.HiDPI.desktop"
    install -Dm644 "$srcdir/runelite.png" "$pkgdir/usr/share/icons/hicolor/128x128/apps/runelite-hidpi.png"
    install -Dm644 "$srcdir/xwayland-$_xwaylandver/COPYING" "$pkgdir/usr/share/licenses/$pkgname/Xwayland-COPYING"
    install -Dm644 "$srcdir/RuneLite-LICENSE" "$pkgdir/usr/share/licenses/$pkgname/RuneLite-LICENSE"
}
sha256sums+=('02593be86e1c4920c8710c698edce26fc2a577588994119a85889b9ca3071315')
sha256sums+=('853b66b4008a009ec91836f79f0e309334884ed96910b9c98ebfeab35adf7c13')
sha256sums+=('a6f65215fd61d052a9a739100f268a7fcd5ba51a8ca0ef8da93563b4ffb6c462')
sha256sums+=('81cb6ce7d8c4b9154e9840ab9d2938d0e6234f227049f004cacf90724f95cc11')
sha256sums+=('ae06ee6dbbacfdd1298271c14bb630b434a34e3e434bc46d05b0301b318adac8')
sha256sums+=('dd35829a586785621651857262168bd91374a84669592d2db28520648b55a519')
