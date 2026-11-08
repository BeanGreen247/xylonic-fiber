#!/usr/bin/env python3
"""Assemble Linux iOS signing assets for Xylonic Fiber into $XF_SIGN_DIR (default ~/.xylonic-fiber-sign/) from what is already on this machine:

  * private key   - read from the Secret Service keyring, where iLoader (github.com/nab138/iloader) stashes it under service "iloader"
  * certificate   - the leaf in DeveloperCertificates[] inside a .mobileprovision
  * profile       - the .mobileprovision issued for org.xylonic.fiber, pulled live off the iPhone with `pymobiledevice3 provision dump`

The phone can hold several profiles (the old Xylonic app, other sideloaded apps): only the one whose app id contains $XF_BUNDLE_MATCH
(default org.xylonic.fiber) is used, so signing never takes over another app's id and replaces that app.

Run it whenever the profile has been refreshed (after an iLoader install). The 1-year "iPhone Developer" cert is stable; only the 7-day
profile churns. Only metadata is printed, never key or certificate bytes.

Requires: python3-gi + gir1.2-secret-1, openssl, pymobiledevice3, a running `sudo pymobiledevice3 remote tunneld`.
"""
import base64
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

OUT_DIR = os.path.expanduser(os.environ.get("XF_SIGN_DIR", "~/.xylonic-fiber-sign"))
P12_PASS = os.environ.get("XF_P12_PASS", "CHANGE_ME")  # sample; wraps a local-only file, set your own
KEYRING_SERVICE = os.environ.get("XF_KEYRING_SERVICE", "iloader")
WANT = os.environ.get("XF_BUNDLE_MATCH", "org.xylonic.fiber")


def sh(*args, **kw):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kw)


def die(msg):
    sys.exit(f"ios-extract-signing: {msg}")


def parse_profile(path):
    raw = open(path, "rb").read()
    m = re.search(rb"<\?xml.*</plist>\s*", raw, re.S)
    if m:
        return plistlib.loads(m.group(0))
    return plistlib.loads(sh("openssl", "smime", "-inform", "DER", "-verify", "-noverify", "-in", path).stdout.encode())


os.makedirs(OUT_DIR, mode=0o700, exist_ok=True)

# 1. provisioning profile: live from the device, the newest one issued for Fiber
prof = None
prof_path = os.path.join(OUT_DIR, "app.mobileprovision")
with tempfile.TemporaryDirectory() as td:
    try:
        sh("pymobiledevice3", "provision", "dump", td)
    except FileNotFoundError:
        die("pymobiledevice3 not on PATH")
    except subprocess.CalledProcessError as e:
        die(f"`pymobiledevice3 provision dump` failed - is the RSD tunnel up?\n{e.stderr}")
    dumped = sorted(f for f in os.listdir(td) if f.endswith(".mobileprovision"))
    if not dumped:
        die("no provisioning profile on the device - install the Fiber IPA once with iLoader first")
    found = []
    for f in dumped:
        try:
            p = parse_profile(os.path.join(td, f))
        except Exception:
            continue
        app_id = p.get("Entitlements", {}).get("application-identifier", "")
        found.append(app_id)
        if WANT in app_id and (prof is None or p.get("ExpirationDate") > prof.get("ExpirationDate")):
            prof = p
            shutil.copyfile(os.path.join(td, f), prof_path)
            os.chmod(prof_path, 0o600)
    if prof is None:
        die(f"no profile for '{WANT}' on the device (found: {', '.join(found) or 'none'}).\n"
            "Install the unsigned Fiber IPA once with iLoader (it creates the app id and the profile), then run this again.")

certs = prof.get("DeveloperCertificates") or []
if not certs:
    die("provisioning profile has no DeveloperCertificates")

# 2. private key: from the keyring
import gi
gi.require_version("Secret", "1")
from gi.repository import Secret

schema = Secret.Schema.new(
    "org.xylonic.fiber.iossign", Secret.SchemaFlags.DONT_MATCH_NAME,
    {"service": Secret.SchemaAttributeType.STRING,
     "application": Secret.SchemaAttributeType.STRING,
     "username": Secret.SchemaAttributeType.STRING,
     "target": Secret.SchemaAttributeType.STRING},
)
svc = Secret.Service.get_sync(Secret.ServiceFlags.LOAD_COLLECTIONS, None)
items = svc.search_sync(
    schema, {"service": KEYRING_SERVICE},
    Secret.SearchFlags.ALL | Secret.SearchFlags.UNLOCK | Secret.SearchFlags.LOAD_SECRETS,
    None,
)
key_item = next((i for i in items if i.get_attributes().get("username", "").endswith("/key")), None)
if not key_item:
    die(f"no '<hash>/key' entry under keyring service '{KEYRING_SERVICE}' (run an iLoader install so it provisions a key)")
key_der = base64.b64decode(re.sub(r"\s+", "", bytes(key_item.get_secret().get()).decode("ascii")))

# 3. build cert.p12
with tempfile.TemporaryDirectory() as td:
    kd, kp = os.path.join(td, "k.der"), os.path.join(td, "k.pem")
    cd, cp = os.path.join(td, "c.der"), os.path.join(td, "c.pem")
    open(kd, "wb").write(key_der)
    open(cd, "wb").write(certs[0])
    try:
        sh("openssl", "pkcs8", "-inform", "DER", "-nocrypt", "-in", kd, "-out", kp)
    except subprocess.CalledProcessError:
        sh("openssl", "rsa", "-inform", "DER", "-in", kd, "-out", kp)
    sh("openssl", "x509", "-inform", "DER", "-in", cd, "-out", cp)

    km = sh("openssl", "rsa", "-in", kp, "-noout", "-modulus").stdout.strip()
    cm = sh("openssl", "x509", "-in", cp, "-noout", "-modulus").stdout.strip()
    if km != cm:
        die("keyring key does not match the cert in the current profile - run a fresh iLoader install, then retry")

    dates = sh("openssl", "x509", "-in", cp, "-noout", "-subject", "-dates").stdout.strip()
    p12 = os.path.join(OUT_DIR, "cert.p12")
    sh("openssl", "pkcs12", "-export", "-legacy", "-inkey", kp, "-in", cp,
       "-name", "xylonic-fiber-ios", "-out", p12, "-passout", f"pass:{P12_PASS}")
    os.chmod(p12, 0o600)

open(os.path.join(OUT_DIR, "p12.pass"), "w").write(P12_PASS + "\n")
os.chmod(os.path.join(OUT_DIR, "p12.pass"), 0o600)

app_id = prof["Entitlements"].get("application-identifier", "")
bundle_id = app_id.split(".", 1)[1] if "." in app_id else ""
open(os.path.join(OUT_DIR, "bundle_id"), "w").write(bundle_id + "\n")

print("signing assets written to", OUT_DIR)
for line in dates.splitlines():
    print("  " + line)
print(f"  profile expires: {prof.get('ExpirationDate')}")
print(f"  bundle id      : {bundle_id}")
