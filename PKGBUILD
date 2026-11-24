# Maintainer: Pablo Barraza <pablo.barraza@protonmail.com>
# ratslap-g305x — minimal C CLI for the Logitech G305 X (GPL-3.0-or-later)
#
# Run makepkg straight from a git clone of the repo: every source is a
# single flat file, because makepkg stages local sources into ./src by
# file name only (directories are not valid source entries, and a src/
# directory in the tree would collide with makepkg's work dir). Do not
# reintroduce nested source directories.

pkgname=ratslap-g305x
pkgver=0.1.2
pkgrel=1
pkgdesc="Configure the Logitech G305 X on Linux: buttons, DPI, report rate, profiles (HID++, no daemon)"
arch=('x86_64')
url="https://github.com/pablobc-mx/ratslap-g305x"
license=('GPL-3.0-or-later')
depends=('glibc')
makedepends=('gcc' 'make')
source=("Makefile" "util.h" "hidpp.h" "hidpp.c" "onboard.h" "onboard.c" \
        "actions.h" "actions.c" "main.c" \
        "test_crc.c" "test_hidpp.c" "test_profile.c" "test_actions.c" \
        "mock_hidraw.c" "golden.h" "gen_golden.py" "integration.sh" \
        "42-g305x.rules" "README.md" "LICENSE")
sha256sums=('9af6c0df7b74eade12afb4a97a14e813845f9330b54daca5293acf2488d7b4bf'
            '137bb80993d42431d70c4303bc34bd067adea2aee5b06bd22f8341a9c20c3a16'
            '58a5f0194db5784eac71c7d4c18e0a2624c037b8df5a8686c11384d932a52bd7'
            '3c0a13ab981d15dab7671aad438e86c5bacd797b91936167c999b2814cceef2a'
            '45f2acc96fc4b520d51c514a124548f8412db10d09d96445efcdfa90f44b9e86'
            '6ff3b4924e4f1b58668098d0b9b591dc4041600ad0d01a6eb7c5eac3cba16322'
            '062ee159bfe85502866a2afb390d3b028a48e206369e3cc86694a8cd037546b6'
            '8f845d771e4c74ccabb5a8f188c8afe36cf2c99dff6abfed06d5a9b7cc159fe0'
            '52faff5ebfecf39d7274a67b4d0ad6b05d43b7d4c3f4c62bed90a09789ad5400'
            '7e1ca5a9cf42c3d7bf6eb860752b4e67ef786a2b896e1958656cb87490855129'
            '087d2521d171a48ef00553914ba50dee5e29095c8d4b57111572a4ec973e35e4'
            '22c90a4d2829dbeaad46902508a2d4e9419ea4ac10dc725b78dff58e762555a7'
            '088609144ced62fb4e4e196ca4747c2ea6505c6fa13c2a8ec536d7c91b3cc871'
            '6c66187bfaf4032290e18f4fb4740fbc1370fbf56cfdbfa4395c7e16c78c0ac7'
            '354559853b32e2c0b3d473d7f82302a898102c611ef8bd39a6c8c364f302030c'
            'a242112632dd0687af0981b7e722e294232df839b79454baef5904ff087ff281'
            'cfa279e379193c3200bad0353b018284e4b3c790b4fc231f40ff2e110ea8799a'
            'b31a2891b6f6cf24965cfc74332afed5b5092339068a0d9685a7cd3a8eab9831'
            'f625527609234591a9f9501fec7a3852a30a667b6808319a1f27af7d2284c5cf'
            '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986')

build() {
    make
}

check() {
    make test
}

package() {
    install -Dm755 "$srcdir/ratslap-g305x" "$pkgdir/usr/bin/ratslap-g305x"
    install -Dm644 "$srcdir/42-g305x.rules" "$pkgdir/usr/lib/udev/rules.d/42-g305x.rules"
    install -Dm644 "$srcdir/README.md" "$pkgdir/usr/share/doc/ratslap-g305x/README.md"
    install -Dm644 "$srcdir/LICENSE" "$pkgdir/usr/share/licenses/ratslap-g305x/LICENSE"
}
