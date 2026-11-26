# Maintainer: Pablo Barraza <pablo.barraza@protonmail.com>
# ratslap-g305x — minimal C CLI for the Logitech G305 X (GPL-3.0-or-later)
#
# Run makepkg straight from a git clone of the repo: every source is a
# single flat file, because makepkg stages local sources into ./src by
# file name only (directories are not valid source entries, and a src/
# directory in the tree would collide with makepkg's work dir). Do not
# reintroduce nested source directories.

pkgname=ratslap-g305x
pkgver=0.1.7
pkgrel=1
pkgdesc="Configure Logitech G305 X and G300/G300s mice on Linux: buttons, DPI, report rate, profiles (no daemon)"
arch=('x86_64')
url="https://github.com/pablobc-mx/ratslap-g305x"
license=('GPL-3.0-or-later')
depends=('glibc')
makedepends=('gcc' 'make')
source=("Makefile" "util.h" "hidpp.h" "hidpp.c" "onboard.h" "onboard.c" "actions.h" "actions.c" "g300s.h" "g300s.c" \
        "main.c" "test_crc.c" "test_hidpp.c" "test_profile.c" "test_actions.c" "test_g300s.c" "mock_hidraw.c" "golden.h" "gen_golden.py" "integration.sh" "42-g305x.rules" "README.md" "LICENSE")
sha256sums=('faec4791e74185efd916434a1562589cc0333ad2faf4e1f52548892b83ded22b'
            '137bb80993d42431d70c4303bc34bd067adea2aee5b06bd22f8341a9c20c3a16'
            '520872a4711ec8de3e0b10696de0d19eb8aa02ca257650d3372e34dc997a921b'
            'f8ee7efda54df1755cb2cd642ac85f2c377e084329bbc22e277351266b332ab2'
            '45f2acc96fc4b520d51c514a124548f8412db10d09d96445efcdfa90f44b9e86'
            '94cc7fd9107fdef58265ff55f4768b673350772350f5fb95e1b30e862ce03c9c'
            '062ee159bfe85502866a2afb390d3b028a48e206369e3cc86694a8cd037546b6'
            '8f845d771e4c74ccabb5a8f188c8afe36cf2c99dff6abfed06d5a9b7cc159fe0'
            '4dda8d17dea240ac8240ea8a76d4e6a2bb4ed3e25e5e8307b2a44a36d4a34ba5'
            'c72de748e2d0ca785fd2eb00a887e789696d66311a31611fcd03960bd72e9dce'
            '2c3e450140b7044c95ba762effb2b9b04420690f92135db3b8d4bd5a6bf46433'
            '1526aed6243170832dbb4c37f26fd76a1eff03652920cdadad60c35e97037afb'
            '087d2521d171a48ef00553914ba50dee5e29095c8d4b57111572a4ec973e35e4'
            '22c90a4d2829dbeaad46902508a2d4e9419ea4ac10dc725b78dff58e762555a7'
            '088609144ced62fb4e4e196ca4747c2ea6505c6fa13c2a8ec536d7c91b3cc871'
            '12e9752304253308cb7f84bd251484a865a4884823c6dc9ed932b908047a2f37'
            'bf7d9b09a438e1c215db6af994b8d7c0fb9478ac713a0ef893ad11beeaa227b2'
            'f023c6249a101d27ad446ba22bf371b1b63d027a03daa121fb3df991b421c2d4'
            '8963cfa1ef15da4aac750b032b2acce6d6df1f08bc3f444737c8c09aa2577106'
            '9d541b101225277fa81c77b990e825cc25024f1c79f9b49f3813c9077eb90ffc'
            'ff8f71a703c5133c8cc504e0a21961730b3f8646b09949e7f8af701bf61786ad'
            '5dc2b29048131ec4c75566e9e8a80f17ddb1aeaee9965becdc0538530071f9ed'
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
