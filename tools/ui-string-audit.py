#!/usr/bin/env python3
"""Lists user-visible strings of the original React source that do not occur in the native C++ sources.
Usage: tools/ui-string-audit.py [original-src-dir] (default /home/bean/xylonic/src)"""
import collections, os, re, sys
orig = sys.argv[1] if len(sys.argv) > 1 else '/home/bean/xylonic/src'
here = os.path.dirname(os.path.abspath(__file__))
native = ''
for root, _, fs in os.walk(os.path.join(here, '..', 'src')):
    for f in fs:
        if f.endswith(('.cpp', '.h')): native += open(os.path.join(root, f), errors='ignore').read() + '\n'
missing = collections.OrderedDict()
for root, _, fs in os.walk(orig):
    for f in sorted(fs):
        if not f.endswith('.tsx') or 'test' in f: continue
        path = os.path.join(root, f); text = open(path, errors='ignore').read(); strs = set()
        for m in re.finditer(r'>\s*([A-Z][A-Za-z0-9 ,.\'’!?:&/()—–-]{3,60}?)\s*<', text): strs.add(m.group(1).strip())
        for m in re.finditer(r'(?:title|placeholder|aria-label)=["\']([A-Z][^"\']{3,70})["\']', text): strs.add(m.group(1))
        miss = sorted(s for s in strs if s not in native and s.replace('’', "'") not in native and '{' not in s and not re.fullmatch(r'[A-Z][a-z]+', s))
        if miss: missing[os.path.relpath(path, orig)] = miss
for p, m in missing.items():
    print(p, len(m))
    for s in m: print('   ', s)
print('total', sum(len(m) for m in missing.values()))
