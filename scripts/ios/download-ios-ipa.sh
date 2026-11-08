#!/usr/bin/env bash
# Download the latest IPA that CI built (ios.yml, or the release workflow that calls it).
# Requires: gh CLI authenticated (gh auth login), run from a clone of the repo.
# Usage: bash scripts/ios/download-ios-ipa.sh [output-dir]
#
# The IPA is unsigned: sign and install it with scripts/ios/ios-autoload.sh (Linux, USB iPhone) or Sideloadly / AltStore (Windows, macOS).
# See docs/CI.md.
set -euo pipefail

OUTDIR="${1:-./ios-artifact}"
ARTIFACT="ios-ipa"

command -v gh >/dev/null || { echo "Error: gh CLI not found (sudo apt install gh, or https://cli.github.com)" >&2; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "Error: gh is not authenticated: run gh auth login" >&2; exit 1; }

echo "Looking for the latest successful iOS build..." >&2
RUN_ID=""
for WF in ios.yml release.yml; do
  RUN_ID=$(gh run list --workflow "$WF" --status success --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || echo "")
  [ -n "$RUN_ID" ] && break
done
if [ -z "$RUN_ID" ]; then
  echo "No successful iOS build found. Start one: Actions > iOS > Run workflow (or push a v* tag)." >&2
  exit 1
fi
echo "Found run: $RUN_ID" >&2

mkdir -p "$OUTDIR"
gh run download "$RUN_ID" --name "$ARTIFACT" --dir "$OUTDIR"
IPA=$(find "$OUTDIR" -name '*.ipa' | head -1)
[ -n "$IPA" ] || { echo "Downloaded the artifact but found no .ipa in it." >&2; exit 1; }

echo "IPA ready: $IPA" >&2
echo "Next: bash scripts/ios/ios-autoload.sh   (Linux, USB iPhone), or open it in Sideloadly / AltStore" >&2
