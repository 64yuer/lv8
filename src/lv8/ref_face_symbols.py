"""Generated from the reference symbol-face probe (G4.312, M8):
work/reference-traces/_m8_protoface.json - per-prototype Symbol.iterator /
Symbol.unscopables ground truth plus iterator removals and tag
enumerability fixes.  Symbols are keyed rows consumed by the
REF_FAMILY_FACE_JS installer (kinds: sym-it / sym-usc / sym-del / tagne).

Do not hand-edit; regenerate from the probe after re-measuring.
"""

from __future__ import annotations

__all__ = ["REF_FACE_SYMBOLS"]

# [name, member, kind, param, value, enumerable]
REF_FACE_SYMBOLS = [
    # --- Symbol.iterator: reference reuses Array.prototype[Symbol.iterator]
    # (name "values", native) on the list/collection interfaces ---
    ["DOMStringList", "@iterator", "sym-it", "arr", None, False],
    ["DOMTokenList", "@iterator", "sym-it", "arr", None, False],
    ["NodeList", "@iterator", "sym-it", "arr", None, False],
    ["NamedNodeMap", "@iterator", "sym-it", "arr", None, False],
    ["HTMLCollection", "@iterator", "sym-it", "arr", None, False],
    ["HTMLAllCollection", "@iterator", "sym-it", "arr", None, False],
    ["HTMLOptionsCollection", "@iterator", "sym-it", "arr", None, False],
    ["SVGTransformList", "@iterator", "sym-it", "arr", None, False],
    ["HTMLFormElement", "@iterator", "sym-it", "arr", None, False],
    ["CSSKeyframesRule", "@iterator", "sym-it", "arr", None, False],
    ["CSSStyleDeclaration", "@iterator", "sym-it", "arr", None, False],
    ["FileList", "@iterator", "sym-it", "arr", None, False],
    ["TextTrackList", "@iterator", "sym-it", "arr", None, False],
    ["DOMRectList", "@iterator", "sym-it", "arr", None, False],
    ["CSSRuleList", "@iterator", "sym-it", "arr", None, False],
    ["CSSNumericArray", "@iterator", "sym-it", "arr", None, False],
    ["CSSUnparsedValue", "@iterator", "sym-it", "arr", None, False],
    ["SVGLengthList", "@iterator", "sym-it", "arr", None, False],
    ["SVGNumberList", "@iterator", "sym-it", "arr", None, False],
    ["SVGPointList", "@iterator", "sym-it", "arr", None, False],
    ["SVGStringList", "@iterator", "sym-it", "arr", None, False],
    ["StyleSheetList", "@iterator", "sym-it", "arr", None, False],
    ["TextTrackCueList", "@iterator", "sym-it", "arr", None, False],
    # --- Symbol.iterator: anonymous native (Highlight/XR sets) ---
    ["XRAnchorSet", "@iterator", "sym-it", "anon", None, False],
    ["XRInputSourceArray", "@iterator", "sym-it", "anon", None, False],
    ["Highlight", "@iterator", "sym-it", "anon", None, False],
    ["HighlightRegistry", "@iterator", "sym-it", "anon", None, False],
    # --- Symbol.iterator: anonymous native, enumerable (map-like sets) ---
    ["CustomStateSet", "@iterator", "sym-it", "anonE", None, True],
    ["AudioParamMap", "@iterator", "sym-it", "anonE", None, True],
    # --- Symbol.iterator: "entries"-named native ---
    ["EventCounts", "@iterator", "sym-it", "entries", None, False],
    ["FormData", "@iterator", "sym-it", "entries", None, False],
    ["URLSearchParams", "@iterator", "sym-it", "entries", None, False],
    # --- Symbol.iterator removals (the reference keeps these protos
    # non-iterable; lv8's shim layers added one) ---
    ["Headers", "@iterator", "sym-del", None, None, False],
    ["DataTransferItemList", "@iterator", "sym-del", None, None, False],
    # --- Symbol.unscopables (objects, non-enumerable) ---
    ["CharacterData", "@unscopables", "sym-usc",
     "after|before|remove|replaceWith", None, False],
    ["DocumentFragment", "@unscopables", "sym-usc",
     "append|prepend|replaceChildren", None, False],
    ["DocumentType", "@unscopables", "sym-usc",
     "after|before|remove|replaceWith", None, False],
    ["Element", "@unscopables", "sym-usc",
     "after|append|before|prepend|remove|replaceChildren|replaceWith|slot",
     None, False],
    ["Document", "@unscopables", "sym-usc",
     "append|fullscreen|prepend|replaceChildren", None, False],
    # --- Symbol.toStringTag enumerability fixes (reference keeps the tag
    # non-enumerable; lv8's installs read e:true) ---
    ["CSSStyleDeclaration", "@tag", "tagne", None, None, False],
    ["Element", "@tag", "tagne", None, None, False],
    ["HTMLFormElement", "@tag", "tagne", None, None, False],
    # --- own-symbol order normalization: the reference inserts the tag
    # before iterator/unscopables on these prototypes (gOPN order) ---
    ["Document", "@order", "sym-order", None, None, False],
    ["NodeList", "@order", "sym-order", None, None, False],
    ["Element", "@order", "sym-order", None, None, False],
    ["HTMLFormElement", "@order", "sym-order", None, None, False],
    ["CSSStyleDeclaration", "@order", "sym-order", None, None, False],
    ["URLSearchParams", "@order", "sym-order", None, None, False],
]
