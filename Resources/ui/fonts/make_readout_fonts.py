# Encore — live guitar rig host
# Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
#
# Makes "Inter Readout": Inter with tabular figures and the slashed zero as the
# default glyphs, for readouts (status bar, dB, ms, PC and cue numbers). JUCE
# can't switch on OpenType features, so the `tnum` and `zero` substitutions are
# baked into the character map. Same glyphs, metrics and licence as Inter (OFL;
# the Reserved Font Name rule doesn't apply: Inter has none).
#
#   python3 -I Resources/ui/fonts/make_readout_fonts.py Resources/ui/fonts
#
# Needs fontTools. The output is committed; run this again only when Inter changes.

import sys
from pathlib import Path

from fontTools.ttLib import TTFont

WEIGHTS = ["Regular", "Medium", "SemiBold", "Bold", "ExtraBold"]
FAMILY = "Inter Readout"


def single_substitutions(font, feature_tag):
    """Every single substitution (glyph -> glyph) the feature makes."""
    gsub = font["GSUB"].table
    lookups = set()
    for record in gsub.FeatureList.FeatureRecord:
        if record.FeatureTag == feature_tag:
            lookups.update(record.Feature.LookupListIndex)

    mapping = {}
    for index in sorted(lookups):
        lookup = gsub.LookupList.Lookup[index]
        for sub in lookup.SubTable:
            if lookup.LookupType == 7:   # extension
                sub = sub.ExtSubTable
            if hasattr(sub, "mapping"):
                mapping.update(sub.mapping)
    return mapping


def make(source, target):
    font = TTFont(source)
    tnum = single_substitutions(font, "tnum")
    zero = single_substitutions(font, "zero")
    if not tnum or not zero:
        sys.exit(f"{source}: no tnum or zero substitutions")

    for table in font["cmap"].tables:
        if not table.isUnicode():
            continue
        for codepoint, glyph in list(table.cmap.items()):
            new = tnum.get(glyph, glyph)
            if codepoint == 0x30:
                new = zero.get(new, zero.get(glyph, new))
            table.cmap[codepoint] = new

    style = source.stem.split("-", 1)[1]
    names = font["name"]
    for record in list(names.names):
        if record.nameID in (1, 16):
            record.string = FAMILY
        elif record.nameID == 4:
            record.string = f"{FAMILY} {style}"
        elif record.nameID == 6:
            record.string = f"InterReadout-{style}"
        elif record.nameID == 3:
            record.string = f"{FAMILY} {style}; derived from Inter"

    font.save(target)


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    for weight in WEIGHTS:
        make(folder / f"Inter-{weight}.ttf", folder / f"InterReadout-{weight}.ttf")
        print(f"InterReadout-{weight}.ttf")


if __name__ == "__main__":
    main()
