# Maintainer: Pablo Barraza <pablo.barraza@protonmail.com>
# ratslap-g305x — minimal C CLI for the Logitech G305 X (GPL-3.0-or-later)

pkgname=ratslap-g305x
pkgver=0.1.0
pkgrel=1
pkgdesc="Configure the Logitech G305 X on Linux: buttons, DPI, report rate, profiles (HID++, no daemon)"
arch=('x86_64')
url="https://github.com/pablobc-mx/ratslap-g305x"
license=('GPL-3.0-or-later')
depends=('glibc')
makedepends=('gcc' 'make')
source=("src" "Makefile" "42-g305x.rules" "README.md" "LICENSE")
b2sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

build() {
    make -C "$srcdir"
}

check() {
    make -C "$srcdir" test
}

package() {
    install -Dm755 "$srcdir/ratslap-g305x" "$pkgdir/usr/bin/ratslap-g305x"
    install -Dm644 "$srcdir/42-g305x.rules" "$pkgdir/usr/lib/udev/rules.d/42-g305x.rules"
    install -Dm644 "$srcdir/README.md" "$pkgdir/usr/share/doc/ratslap-g305x/README.md"
    install -Dm644 "$srcdir/LICENSE" "$pkgdir/usr/share/licenses/ratslap-g305x/LICENSE"
}
