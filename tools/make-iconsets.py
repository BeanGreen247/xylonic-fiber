#!/usr/bin/env python3
"""Builds the selectable icon fonts in assets/fonts/ from their upstream npm packages.

    mkdir pk && cd pk && npm pack @primer/octicons @phosphor-icons/web material-symbols
    (untar each into pk/x_<name>/package)  python3 tools/make-iconsets.py pk

Every font maps the same Font Awesome codepoints as fa-solid-subset.ttf, so the app swaps sets by
swapping the font file. A glyph a set lacks falls back to Phosphor outlines (octicons) or to the
classic font at run time. Licenses: Octicons MIT, Phosphor MIT, Material Symbols Apache-2.0.
"""
import re, sys, glob, os
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path
from fontTools.ttLib import TTFont

UPM = 1000
# fa codepoint: (octicon, phosphor, material); a leading "-" in a name = filled variant, "~" = mirrored
ROWS = """
f001 - music-notes music_note
f002 search magnifying-glass search
f004 heart-fill heart-fill favorite.fill
f007 person user person
f00c check check check
f00d x x close
f011 - power power_settings_new
f012 broadcast cell-signal-full signal_cellular_alt
f013 gear gear settings
f015 home house home
f019 download download-simple download
f01e redo arrow-clockwise redo
f021 sync arrows-clockwise sync
f022 tasklist list-dashes list_alt
f023 lock lock lock
f028 unmute speaker-high volume_up
f03a list-unordered list list
f03e image image image
f048 ~skip skip-back-fill skip_previous.fill
f04b play play-fill play_arrow.fill
f04c pause pause-fill pause.fill
f051 skip skip-forward-fill skip_next.fill
f053 chevron-left caret-left chevron_left
f054 chevron-right caret-right chevron_right
f055 plus-circle plus-circle add_circle
f057 x-circle x-circle cancel
f058 check-circle check-circle check_circle
f059 question question help
f05a info info info
f05e blocked prohibit block
f060 arrow-left arrow-left arrow_back
f065 screen-full arrows-out open_in_full
f066 screen-normal arrows-in close_fullscreen
f068 dash minus remove
f06a issue-opened warning-circle error
f06c - leaf eco
f06d flame fire local_fire_department
f06e eye eye visibility
f070 eye-closed eye-slash visibility_off
f071 alert warning warning
f072 paper-airplane airplane flight
f073 calendar calendar calendar_month
f074 arrow-switch shuffle shuffle
f077 chevron-up caret-up expand_less
f078 chevron-down caret-down expand_more
f07b file-directory-fill folder folder
f07c file-directory-open-fill folder-open folder_open
f080 graph-bar-vertical chart-bar bar_chart
f0a0 database hard-drive hard_drive
f0ab download arrow-circle-down arrow_circle_down
f0ac globe globe language
f0c0 people users group
f0c1 link link link
f0c2 cloud cloud cloud
f0c5 copy copy content_copy
f0c7 - floppy-disk save
f0ca list-unordered list-bullets format_list_bulleted
f0e2 undo arrow-counter-clockwise undo
f0e7 zap lightning bolt
f0eb light-bulb lightbulb lightbulb
f0ed desktop-download cloud-arrow-down cloud_download
f110 iterations spinner progress_activity
f111 circle circle circle
f11b - game-controller sports_esports
f11c - keyboard keyboard
f127 unlink link-break link_off
f14e telescope compass explore
f15b file file article
f15c file file-text description
f186 moon moon dark_mode
f188 bug bug bug_report
f1b9 - car directions_car
f1ce sync circle-notch progress_activity
f1da history clock-counter-clockwise history
f1de sliders sliders-horizontal tune
f1e6 plug plug power
f1eb - wifi-high wifi
f1f8 trash trash delete
f1fc paintbrush paint-brush brush
f201 graph chart-line show_chart
f233 server hard-drives dns
f240 - battery-full battery_full
f24e law scales balance
f271 calendar calendar-plus edit_calendar
f2bd person-fill user-circle account_circle
f2ed trash trash delete
f2f1 sync arrows-clockwise sync
f2f5 sign-out sign-out logout
f2f6 sign-in sign-in login
f302 image images photo_library
f304 pencil pencil-simple edit
f362 arrow-switch arrows-left-right swap_horiz
f363 loop repeat repeat
f390 device-desktop desktop desktop_windows
f3cd device-mobile device-mobile stay_current_portrait
f3ed shield shield-check shield
f422 screen-normal arrows-in close_fullscreen
f466 package package inventory_2
f500 people users-three groups
f506 person user-minus person_off
f518 book book-open menu_book
f51a - broom cleaning_services
f51f - disc album
f53f paintbrush palette palette
f58e grabber dots-six-vertical drag_indicator
f5fd stack stack layers
f625 meter gauge speed
f6ff - network lan
f7c0 broadcast broadcast satellite_alt
f7e4 flame flame local_fire_department
f7f7 - first-aid healing
f8d9 - vinyl-record album
"""
rows = [r.split() for r in ROWS.strip().splitlines()]


def ph_codes(pk, style):
    css = open(f"{pk}/x_phosphor-icons-web-2.1.2/package/src/{style}/style.css").read()
    return {m.group(1): int(m.group(2), 16) for m in re.finditer(r'\.ph(?:-bold|-fill)?\.ph(?:-bold|-fill)?-([a-z0-9-]+):before\s*\{\s*content:\s*"\\([0-9a-f]+)"', css)}


def outline_pen_scaled(font, gname, box, ascent_shift=0):
    gs = font.getGlyphSet()
    return gs, gname


def place(pen, gs, gname, srcbox):
    s = UPM / srcbox
    # source y-up, box [0,srcbox] (material/phosphor sit on baseline with descender; handled by caller offsets)
    return s


def build(path, family, glyphs):
    """glyphs: {cp: draw(pen)} drawing into a UPM box with origin at the box bottom-left, y up."""
    order = [".notdef"] + ["u%04X" % cp for cp in sorted(glyphs)]
    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({cp: "u%04X" % cp for cp in glyphs})
    tt = {}
    pen = TTGlyphPen(None)
    tt[".notdef"] = pen.glyph()
    for cp, draw in glyphs.items():
        p = TTGlyphPen(None)
        # centre the 1000 box on y = 0 so the app's centring by line height lands on the icon centre
        draw(Cu2QuPen(TransformPen(p, (1, 0, 0, 1, 0, -UPM // 2)), 1.0, reverse_direction=False))
        tt["u%04X" % cp] = p.glyph()
    fb.setupGlyf(tt)
    fb.setupHorizontalMetrics({n: (UPM, 0) for n in order})
    fb.setupHorizontalHeader(ascent=UPM // 2, descent=-UPM // 2)
    fb.setupNameTable({"familyName": family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=UPM // 2, sTypoDescender=-UPM // 2, usWinAscent=UPM // 2, usWinDescent=UPM // 2)
    fb.setupPost()
    fb.save(path)
    print(path, len(glyphs), "glyphs", os.path.getsize(path) // 1024, "KiB")


def phosphor_draws(pk, cache={}):
    out = {}
    for style, suffix in (("regular", ""), ("fill", "-Fill"), ("bold", "-Bold")):
        f = TTFont(f"{pk}/x_phosphor-icons-web-2.1.2/package/src/{style}/Phosphor{suffix}.ttf")
        cm = f.getBestCmap()
        codes = ph_codes(pk, style)
        out[style] = (f.getGlyphSet(), {n: cm[c] for n, c in codes.items() if c in cm}, f["head"].unitsPerEm)
    return out


def ph_draw(ph, style, name):
    gs, names, upm = ph[style]
    g = names.get(name)
    if g is None and style != "regular":
        return ph_draw(ph, "regular", name)
    if g is None:
        return None
    s = UPM / upm
    # phosphor boxes are 256 units sitting 0..upm; descender folded in
    return lambda pen, gs=gs, g=g, s=s: gs[g].draw(TransformPen(pen, (s, 0, 0, s, 0, 0)))


def make_phosphor(pk, dest):
    ph = phosphor_draws(pk)
    glyphs = {}
    for r in rows:
        name = r[2]
        style = "regular"
        if name.endswith("-fill"):
            name, style = name[:-5], "fill"
        d = ph_draw(ph, style, name)
        if d is None:
            print("phosphor missing", r[0], name)
            continue
        glyphs[int(r[0], 16)] = d
    build(dest, "XF Phosphor", glyphs)
    return ph


def svg_draw(path, mirror=False):
    p = parse_path  # noqa
    svg = open(path).read()
    ds = re.findall(r'<path[^>]*\sd="([^"]+)"', svg)
    s = UPM / 24.0
    mat = (-s, 0, 0, -s, UPM, UPM) if mirror else (s, 0, 0, -s, 0, UPM)

    def draw(pen):
        tp = TransformPen(pen, mat)
        for d in ds:
            parse_path(d, tp)
    return draw


def make_octicons(pk, ph, dest):
    base = f"{pk}/x_primer-octicons-19.42.0/package/build/svg"
    glyphs = {}
    for r in rows:
        name = r[1]
        cp = int(r[0], 16)
        mirror = name.startswith("~")
        name = name.lstrip("~-")
        f24 = f"{base}/{name}-24.svg"
        if name in ("skip", "play", "pause"):   # octicons' play/skip are status circles, not transport glyphs
            pname = r[2][:-5]
            glyphs[cp] = ph_draw(ph, "fill", pname)
            continue
        if name and os.path.exists(f24):
            glyphs[cp] = svg_draw(f24, mirror)
            continue
        pname = r[2][:-5] if r[2].endswith("-fill") else r[2]
        d = ph_draw(ph, "regular", pname)
        if d:
            glyphs[cp] = d
        else:
            print("octicons missing", r[0], name)
    build(dest, "XF Octicons", glyphs)


def make_material(pk, dest):
    f = TTFont(f"{pk}/x_material-symbols-0.48.3/package/material-symbols-rounded.woff2")
    gs = f.getGlyphSet()
    order = set(f.getGlyphOrder())
    s = UPM / 960.0 * 1.12   # material's live area is 20 of 24 dp; scale up to match the other sets
    off = UPM * (1 - 1.12) / 2
    glyphs = {}
    for r in rows:
        n = r[3]
        # material em is 960 with the 24dp icon centred on the baseline-shifted box; box bottom is -(960-... ) see below
        if n not in order:
            n = n.replace(".fill", "")
        if n not in order:
            print("material missing", r[0], n)
            continue
        glyphs[int(r[0], 16)] = (lambda pen, n=n: gs[n].draw(TransformPen(pen, (s, 0, 0, s, off, off))))
    build(dest, "XF Material", glyphs)


if __name__ == "__main__":
    pk = sys.argv[1]
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "fonts")
    ph = make_phosphor(pk, f"{out}/icons-phosphor.ttf")
    make_octicons(pk, ph, f"{out}/icons-octicons.ttf")
    make_material(pk, f"{out}/icons-material.ttf")
