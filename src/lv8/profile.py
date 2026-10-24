"""Profile consistency engineering (roadmap doc 5.1/5.2, iteration 2 item L).

The profile layer has one job: turn *caller intent* (a profile name plus
explicit environment overrides) into a **resolved, self-consistent, hashable
browser profile**, and then *check* that the parts are coherent instead of
silently repairing them.

    caller intent (profile name + environment overrides)
        -> ProfileResolver        (derive canonical fields, freeze)
        -> ResolvedBrowserProfile (immutable, serialisable, profile_hash)
        -> validate_profile()     (issues; strict raises, warn records)

Nothing here mutates the environment: the resolver only *reads* the merged
environment that :class:`lv8.engine.JSContext` already built, so a resolved
profile is a pure function of its inputs (same inputs -> same hash).
"""

from __future__ import annotations

import copy
import hashlib
import json
import re
from dataclasses import dataclass, field
from typing import Any, Dict, Iterable, List, Mapping, Optional

__all__ = [
    "PROFILE_SCHEMA_VERSION",
    "ProfileIssue",
    "ProfileValidationError",
    "ResolvedBrowserProfile",
    "resolve_profile",
    "validate_profile",
    "PROFILE_NAMES",
]

PROFILE_SCHEMA_VERSION = 1

#: profile names the resolver accepts (roadmap doc 5.1: only complete profiles)
PROFILE_NAMES = ("default", "chrome-win11-x64-en-us", "reference-exact")

_VALIDATION_MODES = ("strict", "warn", "off")


@dataclass(frozen=True)
class ProfileIssue:
    """One consistency finding.  Never a repair - just a report."""

    code: str
    field: str
    message: str
    severity: str = "error"  # "error" | "warning"

    def as_dict(self) -> Dict[str, Any]:
        return {
            "code": self.code,
            "field": self.field,
            "message": self.message,
            "severity": self.severity,
        }


class ProfileValidationError(ValueError):
    """Raised by ``validation="strict"`` with the structured issue list."""

    def __init__(self, issues: Iterable[ProfileIssue]) -> None:
        self.issues = list(issues)
        detail = "; ".join("%s: %s" % (i.code, i.message) for i in self.issues)
        super().__init__("inconsistent browser profile: " + detail)


@dataclass(frozen=True)
class ResolvedBrowserProfile:
    """The resolved profile handed to the engine (immutable + hashable)."""

    profile_id: str
    schema_version: int
    profile_hash: str
    fields: Dict[str, Any] = field(default_factory=dict)
    derived: Dict[str, Any] = field(default_factory=dict)
    overrides: Dict[str, Any] = field(default_factory=dict)

    def as_dict(self) -> Dict[str, Any]:
        """Diagnostic export (roadmap doc 5.2)."""

        return {
            "profileId": self.profile_id,
            "schemaVersion": self.schema_version,
            "profileHash": self.profile_hash,
            "fields": copy.deepcopy(self.fields),
            "derivedFields": copy.deepcopy(self.derived),
            "overrides": copy.deepcopy(self.overrides),
        }


def _dig(obj: Mapping[str, Any], *path: str, default: Any = None) -> Any:
    node: Any = obj
    for key in path:
        if not isinstance(node, Mapping) or key not in node:
            return default
        node = node[key]
    return node


def _canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, default=str)


def resolve_profile(
    name: Optional[str],
    environment: Mapping[str, Any],
    overrides: Optional[Mapping[str, Any]] = None,
) -> ResolvedBrowserProfile:
    """Resolve *name* + *environment* into a frozen profile.

    ``name`` is the caller's profile selector (``None``/``"default"`` keeps
    the engine's own defaults); ``environment`` is the already-merged
    environment (defaults + caller overrides) that the engine will hand to the
    native layer, so the resolver never changes behaviour - it only describes
    and checks it.
    """

    profile_id = name or "default"
    if profile_id not in PROFILE_NAMES:
        raise ValueError(
            "unknown profile name: %r (expected one of %s)"
            % (name, ", ".join(repr(p) for p in PROFILE_NAMES))
        )

    navigator = _dig(environment, "navigator", default={}) or {}
    screen = environment.get("screen", {}) or {}
    ua = str(navigator.get("userAgent", "") or "")
    brands = navigator.get("userAgentData", {}) or {}
    if isinstance(brands, Mapping):
        brand_list = brands.get("brands", []) or []
    else:  # pragma: no cover - defensive
        brand_list = []

    fields: Dict[str, Any] = {
        "userAgent": ua,
        "appVersion": navigator.get("appVersion"),
        "platform": navigator.get("platform"),
        "brands": copy.deepcopy(brand_list),
        "languages": list(navigator.get("languages", []) or []),
        "language": navigator.get("language"),
        "timezone": environment.get("timezone") or environment.get("timeZone"),
        "screen": {
            "width": screen.get("width"),
            "height": screen.get("height"),
            "availWidth": screen.get("availWidth"),
            "availHeight": screen.get("availHeight"),
            "colorDepth": screen.get("colorDepth"),
            "pixelDepth": screen.get("pixelDepth"),
        },
        "devicePixelRatio": environment.get("devicePixelRatio"),
        "hardwareConcurrency": navigator.get("hardwareConcurrency"),
        "deviceMemory": navigator.get("deviceMemory"),
        "maxTouchPoints": navigator.get("maxTouchPoints"),
    }

    # derived fields: values other groups must agree with (single source)
    derived: Dict[str, Any] = {}
    m = re.search(r"Chrome/(\d+(?:\.\d+){0,3})", ua)
    if m:
        derived["chromeVersion"] = m.group(1)
        derived["chromeMajor"] = m.group(1).split(".")[0]
    os_match = re.search(r"\(([^)]*)\)", ua)
    if os_match:
        derived["uaPlatformToken"] = os_match.group(1)
    if fields["languages"]:
        derived["primaryLanguage"] = fields["languages"][0]
    platform = fields["platform"]
    if isinstance(platform, str) and platform:
        if platform.startswith("Win"):
            derived["osFamily"] = "windows"
        elif platform.startswith("Mac"):
            derived["osFamily"] = "macos"
        elif platform.startswith("Linux"):
            derived["osFamily"] = "linux"
        else:
            derived["osFamily"] = "unknown"
    if derived.get("osFamily") in ("windows", "macos", "linux"):
        derived["arch"] = "x86_64" if "Win64" in ua or "x86_64" in ua else None
    derived["mobile"] = bool(
        isinstance(brands, Mapping) and brands.get("mobile"))
    derived["pointerCoarse"] = bool(
        _dig(environment, "mediaQueries", "(pointer: coarse)"))

    overrides = copy.deepcopy(dict(overrides or {}))
    payload = {
        "profileId": profile_id,
        "schemaVersion": PROFILE_SCHEMA_VERSION,
        "fields": fields,
        "derived": derived,
        "overrides": overrides,
    }
    digest = hashlib.sha256(_canonical_json(payload).encode("utf-8")).hexdigest()
    return ResolvedBrowserProfile(
        profile_id=profile_id,
        schema_version=PROFILE_SCHEMA_VERSION,
        profile_hash="sha256:" + digest[:32],
        fields=fields,
        derived=derived,
        overrides=overrides,
    )


# --------------------------------------------------------------------------
# validators (roadmap doc 5.2, first batch).  Each rule is small, named and
# reported - never repaired.
# --------------------------------------------------------------------------


def _rule_ua_brands_platform(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    issues: List[ProfileIssue] = []
    brands = p.fields.get("brands") or []
    if brands and not any(
            isinstance(b, Mapping) and b.get("brand") in
            ("Chromium", "Google Chrome") for b in brands):
        issues.append(ProfileIssue(
            "ua_brands_missing_chromium", "navigator.userAgentData.brands",
            "brand list lacks Chromium/Google Chrome"))
        return issues
    for brand in brands:
        if not isinstance(brand, Mapping):
            continue
        # GREASE brands ("Not_A Brand", "Not)A;Brand", ...) deliberately use
        # an unrelated version number - only the real Chromium brands must
        # agree with the UA.
        name = str(brand.get("brand", ""))
        if name.startswith("Not") or name.startswith("Not)"):
            continue
        version = str(brand.get("version", ""))
        major = p.derived.get("chromeMajor")
        if major and version and version.split(".")[0] != major:
            issues.append(ProfileIssue(
                "ua_brands_version_mismatch", "navigator.userAgentData.brands",
                "brand %s version %s does not match Chrome/%s in the UA"
                % (brand.get("brand"), version, major)))
    return issues


def _rule_ua_platform_token(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    token = p.derived.get("uaPlatformToken")
    family = p.derived.get("osFamily")
    if not token or family in (None, "unknown"):
        return []
    expected = {
        "windows": "Windows NT",
        "macos": "Macintosh",
        "linux": "X11",
    }[family]
    if expected not in token:
        return [ProfileIssue(
            "ua_platform_token_mismatch", "navigator.userAgent",
            "UA platform token %r does not describe %s" % (token, family))]
    return []


def _rule_languages(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    issues: List[ProfileIssue] = []
    languages = p.fields.get("languages") or []
    language = p.fields.get("language")
    if not languages:
        issues.append(ProfileIssue(
            "languages_empty", "navigator.languages",
            "navigator.languages must not be empty"))
    elif language and languages[0] != language:
        issues.append(ProfileIssue(
            "language_not_primary", "navigator.language",
            "navigator.language %r is not languages[0] %r"
            % (language, languages[0])))
    return issues


def _rule_screen(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    issues: List[ProfileIssue] = []
    screen = p.fields.get("screen") or {}

    def num(key: str) -> Optional[float]:
        value = screen.get(key)
        return float(value) if isinstance(value, (int, float)) else None

    width, height = num("width"), num("height")
    aw, ah = num("availWidth"), num("availHeight")
    if width and aw and aw > width:
        issues.append(ProfileIssue(
            "screen_avail_wider_than_screen", "screen.availWidth",
            "availWidth %s > width %s" % (aw, width)))
    if height and ah and ah > height:
        issues.append(ProfileIssue(
            "screen_avail_taller_than_screen", "screen.availHeight",
            "availHeight %s > height %s" % (ah, height)))
    dpr = p.fields.get("devicePixelRatio")
    if isinstance(dpr, (int, float)) and dpr <= 0:
        issues.append(ProfileIssue(
            "device_pixel_ratio_not_positive", "devicePixelRatio",
            "devicePixelRatio must be > 0, got %r" % (dpr,)))
    depth, pixel = num("colorDepth"), num("pixelDepth")
    if depth and pixel and depth != pixel:
        issues.append(ProfileIssue(
            "color_depth_mismatch", "screen.pixelDepth",
            "colorDepth %s != pixelDepth %s" % (depth, pixel),
            severity="warning"))
    return issues


def _rule_touch_pointer(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    issues: List[ProfileIssue] = []
    mobile = p.derived.get("mobile")
    touch = p.fields.get("maxTouchPoints")
    if mobile and isinstance(touch, (int, float)) and touch <= 0:
        issues.append(ProfileIssue(
            "mobile_without_touch", "navigator.maxTouchPoints",
            "userAgentData.mobile is true but maxTouchPoints is %s" % (touch,)))
    if mobile is False and isinstance(touch, (int, float)) and touch > 0:
        issues.append(ProfileIssue(
            "desktop_with_touch", "navigator.maxTouchPoints",
            "desktop profile declares maxTouchPoints %s" % (touch,),
            severity="warning"))
    return issues


def _rule_hardware(p: ResolvedBrowserProfile) -> List[ProfileIssue]:
    issues: List[ProfileIssue] = []
    cores = p.fields.get("hardwareConcurrency")
    if isinstance(cores, (int, float)) and cores <= 0:
        issues.append(ProfileIssue(
            "hardware_concurrency_not_positive", "navigator.hardwareConcurrency",
            "hardwareConcurrency must be > 0, got %r" % (cores,)))
    memory = p.fields.get("deviceMemory")
    if memory is not None and memory not in (0.25, 0.5, 1, 2, 4, 8):
        issues.append(ProfileIssue(
            "device_memory_outside_spec", "navigator.deviceMemory",
            "deviceMemory %r is not one of the spec values" % (memory,),
            severity="warning"))
    return issues


_RULES = (
    _rule_ua_brands_platform,
    _rule_ua_platform_token,
    _rule_languages,
    _rule_screen,
    _rule_touch_pointer,
    _rule_hardware,
)


def validate_profile(profile: ResolvedBrowserProfile,
                     mode: str = "off") -> List[ProfileIssue]:
    """Run the consistency rules; return the issues (never repairs)."""

    if mode not in _VALIDATION_MODES:
        raise ValueError(
            "validation must be one of %s, got %r"
            % (", ".join(repr(m) for m in _VALIDATION_MODES), mode))
    if mode == "off":
        return []
    issues: List[ProfileIssue] = []
    for rule in _RULES:
        issues.extend(rule(profile))
    if mode == "strict" and any(i.severity == "error" for i in issues):
        raise ProfileValidationError(issues)
    return issues
