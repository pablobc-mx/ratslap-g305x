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
tag="$(git describe --tags --abbrev=0 2>/dev/null)" || {
    echo "no tag found; run: git tag vX.Y.Z" >&2
    exit 1
}

git push "$remote" main
git push "$remote" "$tag"
echo "published main + $tag to $remote"
