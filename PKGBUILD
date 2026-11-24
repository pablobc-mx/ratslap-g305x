# Maintainer: Pablo Barraza <pablo.barraza@protonmail.com>
# ratslap-g305x — minimal C CLI for the Logitech G305 X (GPL-3.0-or-later)
#
# Run makepkg straight from a git clone of the repo: every source is a
# single flat file, because makepkg stages local sources into ./src by
# file name only (directories are not valid source entries, and a src/
# directory in the tree would collide with makepkg's work dir). Do not
# reintroduce nested source directories.

pkgname=ratslap-g305x
pkgver=0.1.3
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
            'fe94bcdc011115eb0e70747cb28baff7f29eb373dc982d4a0c68361777f94dd6'
            'ceed7eb615f65730a586ac0e41435cec0bdce3d44fce0a59ef95223ef09c8f1b'
            '45f2acc96fc4b520d51c514a124548f8412db10d09d96445efcdfa90f44b9e86'
            'b3f6f4a9c5a2165cf632808459e7c5bc6407dc406f8fc03e817bd7c14b9e5b09'
            '062ee159bfe85502866a2afb390d3b028a48e206369e3cc86694a8cd037546b6'
            '8f845d771e4c74ccabb5a8f188c8afe36cf2c99dff6abfed06d5a9b7cc159fe0'
            '7662f6cfd41463b7cdafc728a1ef44ebbc228469f8a6bc6183df58898a010121'
            '7e1ca5a9cf42c3d7bf6eb860752b4e67ef786a2b896e1958656cb87490855129'
            '087d2521d171a48ef00553914ba50dee5e29095c8d4b57111572a4ec973e35e4'
            '22c90a4d2829dbeaad46902508a2d4e9419ea4ac10dc725b78dff58e762555a7'
            '088609144ced62fb4e4e196ca4747c2ea6505c6fa13c2a8ec536d7c91b3cc871'
            'd35c4c30343434c96c13e5e6758282b85bede99049d4684bfce52b8cc8010f25'
            '354559853b32e2c0b3d473d7f82302a898102c611ef8bd39a6c8c364f302030c'
            'a242112632dd0687af0981b7e722e294232df839b79454baef5904ff087ff281'
            'f0fc396bd4965087a97608f41fe20b282b6f739970bac2d1c9144e2832c6466a'
            '754aae16a664eb929dabad5514e9fe1a4d28602ccd98a652ba913be8db228f46'
            '504f90b50832d8f85ce54c1af1567cb3e7af6b8f4c23690181c96a846fa6ce36'
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
