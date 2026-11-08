#!/usr/bin/env bash
# ios-autoload.sh (Xylonic Fiber): one command: pull the latest CI iOS build, sign it for THIS iPhone, install it.
# No Mac, no Windows, no Sideloadly. Same flow as the old Xylonic app's script.
#
#   bash scripts/ios/ios-autoload.sh [work-dir]
#
# It:
#   1. refreshes signing assets in $XF_SIGN_DIR (~/.xylonic-fiber-sign/) from the Secret Service keyring (the private key iLoader put there)
#      plus the provisioning profile for org.xylonic.fiber pulled live off the device         -> ios-extract-signing.py
#   2. downloads the latest unsigned IPA from GitHub Actions                                   -> download-ios-ipa.sh
#   3. signs it with zsign, rewriting the bundle id to the id the profile is for (iLoader mangles it to <bundleid>.<teamid>)
#   4. installs it with `pymobiledevice3 apps install` over the RSD tunnel
#
# Prereqs (one-time):
#   - gh authenticated                       gh auth login
#   - zsign on PATH                          github.com/zhlynn/zsign
#   - pymobiledevice3 on PATH                pipx install pymobiledevice3
#   - python3-gi + gir1.2-secret-1           (keyring read)
#   - RSD tunnel: auto-started (scripts/ios/ios-tunnel.sh, one sudo prompt). Set XF_AUTO_TUNNEL=0 to require a pre-existing one.
#   - the Fiber IPA installed ONCE with iLoader (github.com/nab138/iloader) so the app id org.xylonic.fiber.<team> and its profile exist on the
#     phone; then re-run iLoader whenever the profile is within a day of expiry (free Apple ID: 7 days).
#
# Everything under $XF_SIGN_DIR and every *.ipa is git-ignored. The old app's ~/.xylonic-sign is left alone, so the two apps never share an app id.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SIGN_DIR="${XF_SIGN_DIR:-$HOME/.xylonic-fiber-sign}"
WORK="${1:-$(mktemp -d -t xylonic-fiber-ios-XXXXXX)}"
TUNNELD_URL="${PYMOBILEDEVICE3_TUNNELD_URL:-http://127.0.0.1:49151}"
SKIP_EXTRACT="${XF_SKIP_EXTRACT:-0}"

die()  { printf '\033[0;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[0;36m==>\033[0m %s\n' "$*"; }

command -v gh              >/dev/null || die "gh not found"
command -v zsign           >/dev/null || die "zsign not found (github.com/zhlynn/zsign)"
command -v pymobiledevice3 >/dev/null || die "pymobiledevice3 not found (pipx install pymobiledevice3)"
gh auth status >/dev/null 2>&1        || die "gh not authenticated: gh auth login"

# RSD tunnel: auto-started via ios-tunnel.sh (one sudo prompt if not already up).
if ! curl -sf "$TUNNELD_URL" >/dev/null 2>&1; then
  if [ "${XF_AUTO_TUNNEL:-1}" = "1" ]; then
    bash "$SCRIPT_DIR/ios-tunnel.sh"
  fi
  curl -sf "$TUNNELD_URL" >/dev/null 2>&1 || die "RSD tunnel not up at $TUNNELD_URL: run bash scripts/ios/ios-tunnel.sh"
fi

mkdir -p "$WORK"

# 1. refresh signing assets
if [ "$SKIP_EXTRACT" != "1" ]; then
  info "refreshing signing assets in $SIGN_DIR"
  python3 "$SCRIPT_DIR/ios-extract-signing.py"
fi
P12="$SIGN_DIR/cert.p12"
PROV="$SIGN_DIR/app.mobileprovision"
[ -f "$P12" ] && [ -f "$PROV" ] || die "no signing assets in $SIGN_DIR: run ios-extract-signing.py"
P12_PASS="$(cat "$SIGN_DIR/p12.pass" 2>/dev/null || echo "${XF_P12_PASS:-}")"
BUNDLE_ID="$(cat "$SIGN_DIR/bundle_id" 2>/dev/null || true)"
[ -n "$BUNDLE_ID" ] || die "no bundle_id recorded in $SIGN_DIR (re-run ios-extract-signing.py)"
# (ios-extract-signing.py prints the profile's ExpirationDate; a stale profile surfaces as 0xe8008018 at the install step below.)

# 2. download the latest unsigned IPA
info "downloading the latest iOS build from GitHub Actions"
bash "$SCRIPT_DIR/download-ios-ipa.sh" "$WORK" >/dev/null
UNSIGNED="$(find "$WORK" -maxdepth 2 -name '*.ipa' ! -name 'Fiber-signed.ipa' | head -1)"
[ -n "$UNSIGNED" ] || die "download produced no .ipa"
info "unsigned: $(basename "$UNSIGNED") ($(du -h "$UNSIGNED" | cut -f1))"

# 3. sign (rewrite the bundle id to match the profile)
SIGNED="$WORK/Fiber-signed.ipa"
info "signing as $BUNDLE_ID"
zsign -z 9 -k "$P12" -p "$P12_PASS" -m "$PROV" -b "$BUNDLE_ID" -o "$SIGNED" "$UNSIGNED" || die "zsign failed"
[ -f "$SIGNED" ] || die "zsign produced no output"
info "signed: $(du -h "$SIGNED" | cut -f1)"

# 4. install
info "installing on the device"
pymobiledevice3 apps install "$SIGNED" \
  || die "install failed: if it is 0xe8008018 the profile has expired, run iLoader once with the Fiber IPA, then retry"

info "done: Xylonic Fiber ($BUNDLE_ID) is on the device; installing over an existing copy keeps its data"
[ "${1:-}" ] || info "work dir: $WORK"
