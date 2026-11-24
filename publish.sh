#!/usr/bin/env bash
# publish.sh — push this repository to GitHub: main branch + the v0.1.0 tag.
#
# Usage:
#   ./publish.sh https://github.com/<account>/ratslap-g305x.git
#   ./publish.sh git@github.com:<account>/ratslap-g305x.git
#
# Credentials come from your normal git setup (credential helper, ssh
# agent, or a one-shot askpass); nothing is stored by this script.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <remote-url>" >&2
    exit 1
fi
remote="$1"

cd "$(dirname "$0")"

# sanity: clean tree, tag present
[ -z "$(git status --porcelain)" ] || { echo "git tree is dirty; commit first" >&2; exit 1; }
git rev-parse -q --verify 'refs/tags/v0.1.0' >/dev/null || {
    echo "tag v0.1.0 not found; run: git tag v0.1.0" >&2
    exit 1
}

git push "$remote" main
git push "$remote" v0.1.0
echo "published main + v0.1.0 to $remote"
