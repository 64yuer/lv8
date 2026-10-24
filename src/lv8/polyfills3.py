"""lv8 G3 engine polyfills, tranche 3: DOM element/document surface that the
C++ host layer exposes through the __lv8ElementPatches /
__lv8ElementAccessors / __lv8DocumentPatches registration points, plus
pure-JS globals (workers, streams, event constructors, web audio, canvas).

All deterministic semantics are ported from the reference bootstrap.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_3"]


POLYFILL_TRANCHE_3 = r"""
(function () {
  "use strict";
  const setToStringTag = (object, tag) => Object.defineProperty(
    object, Symbol.toStringTag, { value: tag, configurable: true });

  // ------------------------------------------------ CSS Typed OM + inline style
  // CSSKeywordValue / CSSUnitValue / the CSS namespace are native host
  // constructors now (G4 ledger chunk 2; see LV8DomExtras.cpp). The
  // element-bound style views below stay on this registration surface:
  // their typed values resolve to the native globals at call time.

  const styleDeclarations = target => {
    // JS view over the element's style attribute: property map with
    // cssText serialization (declaration order preserved, per-declaration
    // "!important" priority). A Proxy maps camelCase properties
    // (style.width) onto the kebab-case declarations.
    const parse = text => {
      const map = new Map();
      for (const item of String(text || "").split(";")) {
        const idx = item.indexOf(":");
        if (idx === -1) continue;
        const name = item.slice(0, idx).trim().toLowerCase();
        let value = item.slice(idx + 1).trim();
        let priority = "";
        const bang = value.lastIndexOf("!");
        if (bang !== -1 && value.slice(bang + 1).trim().toLowerCase() === "important") {
          priority = "important";
          value = value.slice(0, bang).trim();
        }
        if (name && value) map.set(name, { value, priority });
      }
      return map;
    };
    const serialize = map => {
      let out = "";
      for (const [name, decl] of map) {
        out += name + ": " + decl.value +
               (decl.priority ? " !" + decl.priority : "") + "; ";
      }
      return out.trimEnd();
    };
    const api = {
      getPropertyValue(name) {
        const decl = parse(target.getAttribute("style")).get(String(name).toLowerCase());
        return decl ? decl.value : "";
      },
      setProperty(name, value, priority) {
        const map = parse(target.getAttribute("style"));
        const key = String(name).toLowerCase();
        if (value === undefined || value === null || value === "") map.delete(key);
        else map.set(key, { value: String(value), priority: priority ? String(priority).toLowerCase() : "" });
        target.setAttribute("style", serialize(map));
        const bridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
        if (bridge && typeof bridge.notifyResize === "function") {
          try { bridge.notifyResize(target); } catch (_) {}
        }
      },
      item(index) { return [...parse(target.getAttribute("style")).keys()][Number(index)] || ""; },
      getPropertyValuePriority(name) {
        const decl = parse(target.getAttribute("style")).get(String(name).toLowerCase());
        return decl ? decl.priority : "";
      },
      getPropertyPriority(name) {
        const decl = parse(target.getAttribute("style")).get(String(name).toLowerCase());
        return decl ? decl.priority : "";
      },
      setPropertyPriority(name, priority) {
        const map = parse(target.getAttribute("style"));
        const key = String(name).toLowerCase();
        const decl = map.get(key);
        if (decl) {
          decl.priority = priority ? String(priority).toLowerCase() : "";
          target.setAttribute("style", serialize(map));
        }
      },
      get length() { return parse(target.getAttribute("style")).size; },
      get cssText() { return serialize(parse(target.getAttribute("style"))); },
      set cssText(text) { target.setAttribute("style", String(text)); },
      [Symbol.iterator]() {
        const entries = [...parse(target.getAttribute("style"))].map(
          ([name, decl]) => [name, decl.value]);
        let index = 0;
        return { next: () => index < entries.length
          ? { done: false, value: entries[index++] }
          : { done: true, value: undefined } };
      },
    };
    // route instances through CSSStyleDeclaration.prototype so
    // `style instanceof CSSStyleDeclaration` holds and prototype members
    // (prototype.getPropertyValue, ...) resolve - the page SDK reads the
    // prototype member directly; its absence fails gate item 37 and the
    // following `undefined.get` read aborts the DOM-build branch.
    // The instance registry is captured by the installed accessors; exposing
    // it on window leaked a non-browser __lv8* global into SDK enumeration.
    const instMap = new WeakMap();
    if (globalThis.CSSStyleDeclaration && typeof globalThis.CSSStyleDeclaration.prototype.setProperty !== "function") {
      const proto = globalThis.CSSStyleDeclaration.prototype;
      const inst = function (decl) { return instMap.get(decl); };
      proto.getPropertyValue = function (name) {
        const a = inst(this);
        return a ? a.getPropertyValue(name) : "";
      };
      proto.setProperty = function (name, value, priority) {
        const a = inst(this);
        if (a) a.setProperty(name, value, priority);
      };
      proto.item = function (index) {
        const a = inst(this);
        return a ? a.item(index) : "";
      };
    }
    // reference-measured: exactly these camelCase names answer `in` on a
    // style instance (work/reference-traces/css-in-names.json).  fooBarQux /
    // MozBoxAlign answer false; webkitTransform / all / x / y / d / r
    // answer true.  The d8y2 collector probes style members with `in`
    // when packing its automation-artifact bitfields, so the trap must
    // match the reference set exactly (G4.222 续五).
    const hasTrapNames = {};
    ("accentColor,alignContent,alignItems,alignSelf,alignmentBaseline,all,anchorName,animationComposition,animationDelay,animationDirection,animationDuration,animationFillMode,animationIterationCount,animationName,animationPlayState,animationRangeEnd,animationRangeStart,animationTimeline,animationTimingFunction,appRegion,appearance,backdropFilter,backfaceVisibility,backgroundAttachment,backgroundBlendMode,backgroundClip,backgroundColor,backgroundImage,backgroundOrigin,backgroundPosition,backgroundRepeat,backgroundSize,baselineShift,baselineSource,blockSize,borderBlockEndColor,borderBlockEndStyle,borderBlockEndWidth,borderBlockStartColor,borderBlockStartStyle,borderBlockStartWidth,borderBottomColor,borderBottomLeftRadius,borderBottomRightRadius,borderBottomStyle,borderBottomWidth,borderCollapse,borderEndEndRadius,borderEndStartRadius,borderImageOutset,borderImageRepeat,borderImageSlice,borderImageSource,borderImageWidth,borderInlineEndColor,borderInlineEndStyle,borderInlineEndWidth,borderInlineStartColor,borderInlineStartStyle,borderInlineStartWidth,borderLeftColor,borderLeftStyle,borderLeftWidth,borderRightColor,borderRightStyle,borderRightWidth,borderStartEndRadius,borderStartStartRadius,borderTopColor,borderTopLeftRadius,borderTopRightRadius,borderTopStyle,borderTopWidth,bottom,boxShadow,boxSizing,breakAfter,breakBefore,breakInside,bufferedRendering,captionSide,caretColor,clear,clip,clipPath,clipRule,color,colorInterpolation,colorInterpolationFilters,colorRendering,colorScheme,columnCount,columnGap,columnRuleColor,columnRuleStyle,columnRuleWidth,columnSpan,columnWidth,containIntrinsicBlockSize,containIntrinsicHeight,containIntrinsicInlineSize,containIntrinsicSize,containIntrinsicWidth,containerName,containerType,content,counterIncrement,counterSet,cssFloat,cssText,cursor,cx,cy,d,direction,display,dominantBaseline,emptyCells,fieldSizing,fill,fillOpacity,fillRule,filter,flexBasis,flexDirection,flexGrow,flexShrink,flexWrap,float,floodColor,floodOpacity,fontFamily,fontKerning,fontOpticalSizing,fontPalette,fontSize,fontStretch,fontStyle,fontSynthesisSmallCaps,fontSynthesisStyle,fontSynthesisWeight,fontVariant,fontVariantAlternates,fontVariantCaps,fontVariantEastAsian,fontVariantLigatures,fontVariantNumeric,fontVariantPosition,fontWeight,gridAutoColumns,gridAutoFlow,gridAutoRows,gridColumnEnd,gridColumnStart,gridRowEnd,gridRowStart,gridTemplateAreas,gridTemplateColumns,gridTemplateRows,height,hyphenateCharacter,hyphenateLimitChars,hyphens,imageOrientation,imageRendering,initialLetter,inlineSize,insetArea,insetBlockEnd,insetBlockStart,insetInlineEnd,insetInlineStart,isolation,justifyContent,justifyItems,justifySelf,left,length,letterSpacing,lightingColor,lineBreak,lineHeight,listStyleImage,listStylePosition,listStyleType,marginBlockEnd,marginBlockStart,marginBottom,marginInlineEnd,marginInlineStart,marginLeft,marginRight,marginTop,markerEnd,markerMid,markerStart,maskClip,maskComposite,maskImage,maskMode,maskOrigin,maskPosition,maskRepeat,maskSize,maskType,mathDepth,mathShift,mathStyle,maxBlockSize,maxHeight,maxInlineSize,maxWidth,minBlockSize,minHeight,minInlineSize,minWidth,mixBlendMode,objectFit,objectPosition,objectViewBox,offsetAnchor,offsetDistance,offsetPath,offsetPosition,offsetRotate,opacity,order,orphans,outlineColor,outlineOffset,outlineStyle,outlineWidth,overflowAnchor,overflowClipMargin,overflowWrap,overflowX,overflowY,overlay,overscrollBehaviorBlock,overscrollBehaviorInline,paddingBlockEnd,paddingBlockStart,paddingBottom,paddingInlineEnd,paddingInlineStart,paddingLeft,paddingRight,paddingTop,paintOrder,parentRule,perspective,perspectiveOrigin,pointerEvents,position,positionAnchor,positionTry,positionTryOptions,positionTryOrder,positionVisibility,r,resize,right,rotate,rowGap,rubyPosition,rx,ry,scale,scrollBehavior,scrollMarginBlockEnd,scrollMarginBlockStart,scrollMarginInlineEnd,scrollMarginInlineStart,scrollPaddingBlockEnd,scrollPaddingBlockStart,scrollPaddingInlineEnd,scrollPaddingInlineStart,scrollTimelineAxis,scrollTimelineName,scrollbarColor,scrollbarGutter,scrollbarWidth,shapeImageThreshold,shapeMargin,shapeOutside,shapeRendering,speak,stopColor,stopOpacity,stroke,strokeDasharray,strokeDashoffset,strokeLinecap,strokeLinejoin,strokeMiterlimit,strokeOpacity,strokeWidth,tabSize,tableLayout,textAlign,textAlignLast,textAnchor,textDecoration,textDecorationColor,textDecorationLine,textDecorationSkipInk,textDecorationStyle,textEmphasisColor,textEmphasisPosition,textEmphasisStyle,textIndent,textOverflow,textRendering,textShadow,textSizeAdjust,textSpacingTrim,textTransform,textUnderlinePosition,textWrap,timelineScope,top,touchAction,transform,transformOrigin,transformStyle,transitionBehavior,transitionDelay,transitionDuration,transitionProperty,transitionTimingFunction,translate,unicodeBidi,userSelect,vectorEffect,verticalAlign,viewTimelineAxis,viewTimelineInset,viewTimelineName,viewTransitionClass,viewTransitionName,visibility,webkitAlignContent,webkitAlignItems,webkitAlignSelf,webkitAnimation,webkitAnimationDelay,webkitAnimationDirection,webkitAnimationDuration,webkitAnimationFillMode,webkitAnimationIterationCount,webkitAnimationName,webkitAnimationPlayState,webkitAnimationTimingFunction,webkitAppRegion,webkitAppearance,webkitBackfaceVisibility,webkitBackgroundClip,webkitBackgroundOrigin,webkitBackgroundSize,webkitBorderBottomLeftRadius,webkitBorderBottomRightRadius,webkitBorderRadius,webkitBorderTopLeftRadius,webkitBorderTopRightRadius,webkitBoxDirection,webkitBoxOrient,webkitBoxShadow,webkitBoxSizing,webkitClipPath,webkitColumnCount,webkitColumnGap,webkitColumnRuleColor,webkitColumnRuleStyle,webkitColumnRuleWidth,webkitColumnSpan,webkitColumnWidth,webkitFilter,webkitFlexBasis,webkitFlexDirection,webkitFlexGrow,webkitFlexShrink,webkitFlexWrap,webkitHyphenateCharacter,webkitJustifyContent,webkitLineBreak,webkitLineClamp,webkitMaskClip,webkitMaskComposite,webkitMaskImage,webkitMaskOrigin,webkitMaskPosition,webkitMaskRepeat,webkitMaskSize,webkitOpacity,webkitOrder,webkitPerspective,webkitPerspectiveOrigin,webkitRubyPosition,webkitShapeImageThreshold,webkitShapeMargin,webkitShapeOutside,webkitTapHighlightColor,webkitTextEmphasisColor,webkitTextEmphasisPosition,webkitTextEmphasisStyle,webkitTextSizeAdjust,webkitTextStroke,webkitTransform,webkitTransformOrigin,webkitTransformStyle,webkitTransition,webkitTransitionDelay,webkitTransitionDuration,webkitTransitionProperty,webkitTransitionTimingFunction,webkitUserSelect,webkitWritingMode,whiteSpaceCollapse,widows,width,willChange,wordBreak,wordSpacing,writingMode,x,y,zIndex,zoom").split(",").forEach(function (n) { hasTrapNames[n] = 1; });
    const proxy = new Proxy(api, {
      has(target, prop) {
        if (typeof prop === "string" && hasTrapNames[prop] === 1) return true;
        return prop in target;
      },
      get(receiver, prop) {
        if (prop in receiver) return receiver[prop];
        if (typeof prop === "string" && /^[a-zA-Z][a-zA-Z0-9]*$/.test(prop)) {
          // reference-measured model: members of the supported set read ""
          // (webkitTransform included); Moz*/Webkit*/ms*/O*/Khtml* and
          // unknown camelCase names read undefined - the SDK's vendor
          // list counts the undefined reads (G4.222 续五 re-measure).
          if (hasTrapNames[prop] === 1) {
            const kebab = prop.replace(/[A-Z]/g, letter => "-" + letter.toLowerCase());
            return api.getPropertyValue(kebab);
          }
          return undefined;
        }
        return undefined;
      },
      set(receiver, prop, value) {
        if (prop === "cssText") { receiver.cssText = value; return true; }
        if (typeof prop === "string" && /^[a-zA-Z][a-zA-Z0-9]*$/.test(prop)) {
          const kebab = prop.replace(/[A-Z]/g, letter => "-" + letter.toLowerCase());
          api.setProperty(kebab, value);
          return true;
        }
        receiver[prop] = value;
        return true;
      },
      // Chrome enumerates only the inline-set properties (an untouched
      // style has Object.keys(style).length === 0 - the api methods are
      // not enumerable members)
      ownKeys(target) {
        const names = [];
        try {
          if (typeof target.getAttribute === "function") {
            for (const name of parse(target.getAttribute("style")).keys()) {
              names.push(name.replace(/-([a-z])/g, (_, c) => c.toUpperCase()));
            }
          }
        } catch (_) {}
        return names;
      },
      getOwnPropertyDescriptor(target, prop) {
        if (typeof prop === "string" &&
            /^[a-zA-Z][a-zA-Z0-9]*$/.test(prop)) {
          const kebab = prop.replace(/[A-Z]/g,
            letter => "-" + letter.toLowerCase());
          const value = api.getPropertyValue(kebab);
          if (value !== "") {
            return { value, enumerable: true, configurable: true,
                     writable: true };
          }
        }
        return undefined;
      },
    });
    instMap.set(proxy, api);
    if (globalThis.CSSStyleDeclaration &&
        Object.getPrototypeOf(proxy) !== globalThis.CSSStyleDeclaration.prototype) {
      try {
        Object.setPrototypeOf(proxy, globalThis.CSSStyleDeclaration.prototype);
      } catch (_) {}
    }
    return proxy;
  };
  const styleMap = (target, computed) => {
    // StylePropertyMap view: typed values round-trip through the style
    // attribute (CSS.px(24).set -> style "24px" -> get -> CSSUnitValue)
    const parse = text => {
      const map = new Map();
      for (const item of String(text || "").split(";")) {
        const idx = item.indexOf(":");
        if (idx === -1) continue;
        const name = item.slice(0, idx).trim().toLowerCase();
        const value = item.slice(idx + 1).trim();
        if (name && value) map.set(name, value);
      }
      return map;
    };
    const toTyped = value => {
      const match = String(value).match(/^(-?(?:\d+|\d*\.\d+))(px|deg|em|rem|%|vh|vw|s|ms|number)$/);
      if (match) return new CSSUnitValue(Number(match[1]), match[2]);
      return new CSSKeywordValue(value);
    };
    return {
      set(name, ...values) {
        const map = parse(target.getAttribute("style"));
        map.set(String(name).toLowerCase(), values.map(v => String(v)).join(" "));
        target.setAttribute("style", [...map].map(([k, v]) => k + ": " + v + ";").join(" "));
      },
      append(name, value) {
        const map = parse(target.getAttribute("style"));
        const key = String(name).toLowerCase();
        map.set(key, (map.has(key) ? map.get(key) + " " : "") + String(value));
        target.setAttribute("style", [...map].map(([k, v]) => k + ": " + v + ";").join(" "));
      },
      get(name) {
        const map = parse(target.getAttribute("style"));
        const raw = map.get(String(name).toLowerCase());
        return raw === undefined ? null : toTyped(raw);
      },
      has(name) { return parse(target.getAttribute("style")).has(String(name).toLowerCase()); },
      delete(name) {
        const map = parse(target.getAttribute("style"));
        map.delete(String(name).toLowerCase());
        target.setAttribute("style", [...map].map(([k, v]) => k + ": " + v + ";").join(" "));
      },
      get size() { return parse(target.getAttribute("style")).size; },
      entries() { return [...parse(target.getAttribute("style")).entries()].map(([k, v]) => [k, toTyped(v)])[Symbol.iterator](); },
      [Symbol.iterator]() { return this.entries(); },
    };
  };
  const parseStyleNumber = (target, property) => {
    const style = String(target.getAttribute("style") || "");
    const match = style.match(new RegExp("(?:^|;)\\s*" + property + "\\s*:\\s*(-?(?:\\d+|\\d*\\.\\d+))px", "i"));
    return match ? Number(match[1]) : 0;
  };

  // ------------------------------------------------ form validity model
  const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
  const validityModel = element => {
    const type = String(element.getAttribute("type") || "").toLowerCase();
    const value = element.value === undefined ? "" : String(element.value);
    const hasRequired = element.hasAttribute("required");
    const state = {
      valueMissing: hasRequired && value === "",
      typeMismatch: type === "email" && value !== "" && !emailPattern.test(value),
      patternMismatch: false,
      tooLong: false,
      tooShort: false,
      rangeUnderflow: false,
      rangeOverflow: false,
      stepMismatch: false,
      tooLong__: false,
      customError: false,
    };
    if (type === "number" && value !== "") {
      const number = Number(value);
      if (!Number.isNaN(number)) {
        if (element.hasAttribute("min")) state.rangeUnderflow = number < Number(element.getAttribute("min"));
        if (element.hasAttribute("max")) state.rangeOverflow = number > Number(element.getAttribute("max"));
        const step = Number(element.getAttribute("step") || 1);
        const min = element.hasAttribute("min") ? Number(element.getAttribute("min")) : 0;
        if (step > 0) state.stepMismatch = Math.abs((number - min) / step - Math.round((number - min) / step)) > 1e-9;
      }
    }
    const keys = ["valueMissing", "typeMismatch", "patternMismatch", "tooLong",
                  "tooShort", "rangeUnderflow", "rangeOverflow", "stepMismatch",
                  "badInput", "customError"];
    let valid = true;
    for (const key of keys) valid = valid && !state[key];
    state.valid = valid;
    return state;
  };
  const formControls = form => {
    const controls = [];
    const visit = node => {
      if (node.nodeType === 1 || node.tagName) {
        const tag = String(node.tagName || "").toLowerCase();
        if (["input", "select", "textarea", "button"].includes(tag)) controls.push(node);
      }
      const children = node.children;
      const count = children ? children.length : 0;
      for (let index = 0; index < count; index += 1) visit(children[index]);
    };
    visit(form);
    return controls;
  };
  const formIsValid = form => formControls(form).every(control => {
    const validity = control.validity;
    return !validity || validity.valid;
  });

  // ------------------------------------------------ dialog / popover state
  const dialogState = new WeakMap();

  // ------------------------------------------------ element patches
  const pendingSlotChanges = new WeakSet();
  const scheduleSlotChange = slotEl => {
    if (pendingSlotChanges.has(slotEl)) return;
    pendingSlotChanges.add(slotEl);
    queueMicrotask(() => {
      pendingSlotChanges.delete(slotEl);
      try { slotEl.dispatchEvent(new Event("slotchange")); } catch (_) {}
    });
  };
  const noteSlotMutation = container => {
    try {
      if (!container) return;
      let root = null;
      if (container.nodeType === 11) root = container;
      else if (container.nodeType === 1) {
        if (container.shadowRoot) root = container.shadowRoot;
        else {
          let node = container;
          while (node.parentNode) node = node.parentNode;
          root = node;
        }
      }
      if (!root || root.nodeType !== 11 || !root.host) return;
      for (const child of root.childNodes || []) {
        if (child.nodeType === 1 && String(child.tagName).toLowerCase() === "slot") {
          scheduleSlotChange(child);
        }
      }
    } catch (_) {}
  };
  globalThis.__lv8_noteSlotMutation = noteSlotMutation;
  const elementPatches = {
    append(...nodes) {
      for (const node of nodes) {
        if (node && typeof node === "object" && typeof node.nodeType === "number" && node.nodeType === 11) {
          for (const child of [...(node.childNodes || [])]) this.appendChild(child);
        } else if (node && typeof node === "object") {
          this.appendChild(node);
        } else {
          this.appendChild(typeof document !== "undefined" ? document.createTextNode(String(node)) : String(node));
        }
      }
      if (typeof noteSlotMutation === "function") noteSlotMutation(this);
    },
    prepend(...nodes) {
      const first = this.firstChild;
      for (const node of nodes.reverse()) {
        if (node && typeof node === "object" && node.nodeType === 11) {
          for (const child of [...(node.childNodes || [])]) this.insertBefore ? this.insertBefore(child, first) : this.appendChild(child);
        } else if (node && typeof node === "object") {
          this.insertBefore ? this.insertBefore(node, first) : this.appendChild(node);
        }
      }
    },
    replaceChildren(...nodes) {
      while (this.firstChild) this.removeChild(this.firstChild);
      this.append(...nodes);
    },
    remove() {
      const parent = this.parentNode;
      if (parent) parent.removeChild(this);
    },
    closest(selector) {
      const wanted = String(selector || "").trim().toLowerCase();
      let node = this;
      let depth = 0;
      while (node && node.nodeType === 1 && depth < 64) {
        if (/^[a-zA-Z][\w-]*$/.test(wanted) &&
            String(node.tagName || "").toLowerCase() === wanted) {
          return node;
        }
        const parent = node.parentNode;
        if (parent === node) break;
        node = parent;
        depth += 1;
      }
      return null;
    },
    before(...nodes) {
      const parent = this.parentNode;
      if (!parent) return;
      const frag = document.createDocumentFragment();
      for (const node of nodes) frag.append(node);
      parent.insertBefore(frag, this);
    },
    after(...nodes) {
      const parent = this.parentNode;
      if (!parent) return;
      const frag = document.createDocumentFragment();
      for (const node of nodes) frag.append(node);
      parent.insertBefore(frag, this.nextSibling);
    },
    replaceWith(...nodes) {
      const parent = this.parentNode;
      if (!parent) return;
      const frag = document.createDocumentFragment();
      for (const node of nodes) frag.append(node);
      parent.replaceChild(frag, this);
    },
    click() {
      const tag = String(this.tagName || "").toLowerCase();
      const type = String(this.getAttribute("type") ||
          (tag === "button" ? "submit" : "text")).toLowerCase();
      if (tag === "input" && type === "checkbox") {
        if (this.hasAttribute("disabled")) return false;
        this.checked = !this.checked;
      }
      const event = typeof Event === "function" ? new Event("click", { bubbles: true, cancelable: true }) : null;
      if (!event) return false;
      if (typeof event.preventDefault !== "function") event.preventDefault = () => { event.defaultPrevented = true; };
      let prevented = false;
      Object.defineProperty(event, "defaultPrevented", { get: () => prevented });
      event.preventDefault = () => { prevented = true; };
      const result = this.dispatchEvent(event);
      // submit buttons trigger form submission through the validity gate
      if (tag === "button" && type === "submit") {
        const form = this.closest ? this.closest("form") : null;
        if (form && form.requestSubmit) form.requestSubmit(this);
      }
      if (tag === "input" && (type === "checkbox" || type === "radio")) {
        this.dispatchEvent(new Event("change", { bubbles: true }));
      }
      return result;
    },
    focus() {
      try { this.dispatchEvent(new Event("focus", { bubbles: false })); } catch (_) {}
      this._focused = true;
    },
    blur() {
      try { this.dispatchEvent(new Event("blur", { bubbles: false })); } catch (_) {}
      this._focused = false;
    },
    checkValidity() { return formIsValid(this); },
    reportValidity() { return formIsValid(this); },
    requestSubmit(submitter = null) {
      const controls = formControls(this);
      const invalid = controls.filter(control => control.validity && !control.validity.valid);
      if (invalid.length) {
        for (const control of invalid) {
          try { control.dispatchEvent(new Event("invalid", { bubbles: false, cancelable: true })); } catch (_) {}
        }
        return false;
      }
      const event = typeof SubmitEvent === "function"
        ? new SubmitEvent("submit", { bubbles: true, cancelable: true, submitter })
        : new Event("submit", { bubbles: true, cancelable: true });
      return this.dispatchEvent(event) ? true : false;
    },
    submit() { return this.requestSubmit(); },
    showModal() {
      const state = dialogState.get(this) || { open: false, returnValue: "" };
      state.open = true;
      dialogState.set(this, state);
      try { this.dispatchEvent(new Event("open", { bubbles: false })); } catch (_) {}
    },
    close(returnValue = undefined) {
      const state = dialogState.get(this) || { open: false, returnValue: "" };
      if (!state.open) return;
      state.open = false;
      if (returnValue !== undefined) state.returnValue = String(returnValue);
      dialogState.set(this, state);
      try { this.dispatchEvent(new Event("close", { bubbles: false })); } catch (_) {}
    },
    requestClose(returnValue = undefined) {
      const state = dialogState.get(this) || { open: false, returnValue: "" };
      if (!state.open) return;
      let prevented = false;
      const cancel = new Event("cancel", { bubbles: false, cancelable: true });
      Object.defineProperty(cancel, "defaultPrevented", { get: () => prevented });
      cancel.preventDefault = () => { prevented = true; };
      this.dispatchEvent(cancel);
      if (prevented) return;
      this.close(returnValue);
    },
    showPopover() {
      const state = dialogState.get(this) || {};
      state.popoverOpen = true;
      dialogState.set(this, state);
      try { this.dispatchEvent(new Event("toggle", { bubbles: false })); } catch (_) {}
    },
    hidePopover() {
      const state = dialogState.get(this) || {};
      if (!state.popoverOpen) return;
      state.popoverOpen = false;
      dialogState.set(this, state);
      try { this.dispatchEvent(new Event("toggle", { bubbles: false })); } catch (_) {}
    },
    togglePopover(force = undefined) {
      const state = dialogState.get(this) || {};
      const shouldShow = force === undefined ? !state.popoverOpen : Boolean(force);
      if (shouldShow) this.showPopover(); else this.hidePopover();
      return shouldShow;
    },
    play() {
      const state = dialogState.get(this) || {};
      state.paused = false;
      dialogState.set(this, state);
      return Promise.resolve();
    },
    pause() {
      const state = dialogState.get(this) || {};
      state.paused = true;
      dialogState.set(this, state);
    },
    // HTMLMediaElement.canPlayType - reference gold answers "maybe" for every
    // input; the challenge SDK registers media.canPlayType as a probe
    // callback and skips the registration entirely when it is missing.
    canPlayType(type) {
      return "maybe";
    },
    // reference offline wheel does no layout: the rect is the zero rect even
    // for styled elements (re-dumped 2026-09-25), while clientWidth and
    // scrollWidth still derive from CSS - mirror that split exactly.
    getBoundingClientRect() {
      return new DOMRect(0, 0, 0, 0);
    },
    getClientRects() { return [this.getBoundingClientRect()]; },
    computedStyleMap() { return styleMap(this, true); },
  };

  const elementAccessors = {
    value: function () {
      const tag = String(this.tagName || "").toLowerCase();
      if (tag === "select") {
        const selected = this.querySelector ? this.querySelector("option[selected]") : null;
        return selected ? selected.value : "";
      }
      if (tag === "input" &&
          String(this.getAttribute("type") || "").toLowerCase() === "file") {
        const files = this.files;
        const first = files && files.length ? files.item(0) : null;
        return first ? "C:\\fakepath\\" + first.name : "";
      }
      return this.getAttribute("value") || "";
    },
    validity: function () { return validityModel(this); },
    validationMessage: function () {
      const validity = validityModel(this);
      return validity.valid ? "" : "Check this field";
    },
    // innerText on non-rendered elements reads the raw child text (same
    // as textContent) - the challenge SDK snapshots script sources with
    // it (reference gold returns the full source here).
    innerText: function () { return this.textContent; },
    style: function () { return styleDeclarations(this); },
    attributeStyleMap: function () { return styleMap(this); },
    // HTMLMetaElement IDL properties (the challenge SDK reads meta.content
    // as a property, not via getAttribute)
    content: function () {
      if (String(this.tagName).toUpperCase() !== 'META') return undefined;
      return this.getAttribute('content');
    },
    name: function () {
      if (String(this.tagName).toUpperCase() !== 'META') return undefined;
      return this.getAttribute('name');
    },
    httpEquiv: function () {
      if (String(this.tagName).toUpperCase() !== 'META') return undefined;
      return this.getAttribute('http-equiv');
    },
    open: function () {
      const state = dialogState.get(this);
      if (state) return Boolean(state.open);
      return this.hasAttribute("open");
    },
    returnValue: function () {
      const state = dialogState.get(this);
      return state ? state.returnValue : "";
    },
    popoverOpen: function () {
      const state = dialogState.get(this);
      return state ? Boolean(state.popoverOpen) : false;
    },
    paused: function () {
      const state = dialogState.get(this);
      return state ? state.paused !== false : true;
    },
    clientWidth: function () {
      const width = parseStyleNumber(this, "width");
      return width ? Math.ceil(width) : 0;
    },
    clientHeight: function () {
      const height = parseStyleNumber(this, "height");
      return height ? Math.ceil(height) : 0;
    },
    // reference gold: offset* reads are all zero (no layout), unlike client*/
    // scroll* which still derive from CSS
    offsetTop: function () { return 0; },
    offsetLeft: function () { return 0; },
    offsetWidth: function () { return 0; },
    offsetHeight: function () { return 0; },
    scrollWidth: function () {
      const width = parseStyleNumber(this, "width");
      return width ? Math.ceil(width) : 0;
    },
    scrollHeight: function () {
      const height = parseStyleNumber(this, "height");
      return height ? Math.ceil(height) : 0;
    },
  };

  // ------------------------------------------------ document patches
  const selectionMap = new WeakMap();
  const makeSelection = document => {
    let anchorNode = null, anchorOffset = 0, focusNode = null, focusOffset = 0;
    const samePoint = () =>
      anchorNode === focusNode && anchorOffset === focusOffset;
    const sel = {
      get anchorNode() { return anchorNode; },
      get anchorOffset() { return anchorOffset; },
      get focusNode() { return focusNode; },
      get focusOffset() { return focusOffset; },
      get isCollapsed() { return anchorNode !== null && samePoint(); },
      get rangeCount() { return anchorNode === null ? 0 : 1; },
      get type() {
        if (anchorNode === null) return "None";
        return samePoint() ? "Caret" : "Range";
      },
      addRange() {},
      collapse(node, offset = 0) {
        anchorNode = node; anchorOffset = Number(offset);
        focusNode = node; focusOffset = Number(offset);
      },
      collapseToEnd() { focusNode = anchorNode; focusOffset = anchorOffset; },
      collapseToStart() { anchorNode = focusNode; anchorOffset = focusOffset; },
      extend(node, offset = 0) { focusNode = node; focusOffset = Number(offset); },
      setBaseAndExtent(baseNode, baseOffset, extentNode, extentOffset) {
        anchorNode = baseNode; anchorOffset = Number(baseOffset);
        focusNode = extentNode; focusOffset = Number(extentOffset);
      },
      selectAllChildren(node) {
        anchorNode = node; anchorOffset = 0;
        focusNode = node;
        focusOffset = node.childNodes ? node.childNodes.length : 0;
      },
      removeAllRanges() {
        anchorNode = null; focusNode = null;
        anchorOffset = 0; focusOffset = 0;
      },
      deleteFromDocument() {},
      containsNode() { return false; },
      getRangeAt() { return document.createRange(); },
      toString() {
        if (anchorNode === null || samePoint()) return "";
        if (anchorNode === focusNode) return String(anchorNode.textContent || "");
        return String(anchorNode.textContent || "");
      },
    };
    if (typeof globalThis.Selection === "function") {
      Object.setPrototypeOf(sel, globalThis.Selection.prototype);
    }
    return sel;
  };
  const documentPatches = {
    getSelection() {
      let sel = selectionMap.get(this);
      if (!sel) {
        sel = makeSelection(this);
        selectionMap.set(this, sel);
      }
      return sel;
    },
    styleSheets: function () {
      // own-property style: computed once per document object
      return this._styleSheets || (this._styleSheets = buildStyleSheets(this));
    },
  };
  const buildStyleSheets = document => {
    const sheets = [];
    const styleNodes = document.getElementsByTagName("style");
    for (let styleIndex = 0; styleIndex < styleNodes.length; styleIndex += 1) {
      const style = styleNodes[styleIndex];
      const text = style.textContent || "";
      const rules = [];
      for (const rule of text.split("}")) {
        const idx = rule.indexOf("{");
        if (idx === -1) continue;
        const selector = rule.slice(0, idx).trim();
        const body = rule.slice(idx + 1).trim();
        if (!selector || !body) continue;
        const styleMap = {};
        for (const item of body.split(";")) {
          const colon = item.indexOf(":");
          if (colon === -1) continue;
          styleMap[item.slice(0, colon).trim().toLowerCase()] = item.slice(colon + 1).trim();
        }
        rules.push({
          selectorText: selector,
          style: {
            getPropertyValue: name => styleMap[String(name).toLowerCase()] || "",
            length: Object.keys(styleMap).length,
          },
          cssText: selector + " { " + body + "; }",
        });
      }
      sheets.push({
        cssRules: rules,
        ownerNode: style,
        insertRule(rule, index = rules.length) { rules.splice(Number(index), 0, { cssText: String(rule), style: { getPropertyValue: () => "" } }); return Number(index); },
      });
    }
    sheets.item = index => sheets[Number(index)] || null;
    return sheets;
  };

  // ------------------------------------------------ offline workers
  // Worker / SharedWorker are native host constructors now (G4 ledger
  // chunk 7b; see LV8Net.cpp InstallWorkers). MessagePortShim had no
  // consumers and was removed.

  // ------------------------------------------------ web audio (deterministic)
  // AudioBuffer/AudioNode/BaseAudioContext/AudioContext/
  // OfflineAudioContext are native host constructors now (G4 ledger
  // chunk 7a; see LV8DomExtras.cpp InstallWebAudio).

  // ------------------------------------------------ streams (deterministic)
  class ReadableStreamDefaultController {
    constructor(stream) { this._stream = stream; }
    enqueue(chunk) { this._stream._chunks.push(chunk); }
    close() { this._stream._closed = true; }
    error(reason) { this._stream._errored = reason || new Error("stream errored"); }
  }
  class ReadableStream {
    constructor(underlyingSource = {}) {
      this._chunks = [];
      this._closed = false;
      this._errored = null;
      this._underlying = underlyingSource;
      this._controller = new ReadableStreamDefaultController(this);
      if (typeof underlyingSource.start === "function") {
        try { underlyingSource.start(this._controller); } catch (_) {}
      }
    }
    get locked() { return Boolean(this._reader); }
    getReader() {
      const stream = this;
      this._reader = { _stream: stream };
      return {
        read() {
          if (stream._chunks.length) return Promise.resolve({ value: stream._chunks.shift(), done: false });
          if (stream._errored) return Promise.reject(stream._errored);
          return Promise.resolve({ value: undefined, done: true });
        },
        cancel() { stream._closed = true; return Promise.resolve(); },
        releaseLock() { stream._reader = null; },
      };
    }
    cancel() { this._closed = true; return Promise.resolve(); }
  }
  class WritableStreamDefaultController {
    constructor() {}
    error() {}
  }
  class WritableStream {
    constructor(underlyingSink = {}) {
      this._underlying = underlyingSink;
      this._closed = false;
    }
    getWriter() {
      const sink = this._underlying;
      return {
        write(chunk) {
          if (typeof sink.write === "function") { try { sink.write(chunk); } catch (_) {} }
          return Promise.resolve();
        },
        close() {
          if (typeof sink.close === "function") { try { sink.close(); } catch (_) {} }
          return Promise.resolve();
        },
        abort() { return Promise.resolve(); },
        releaseLock() {},
      };
    }
  }
  class TransformStream {
    constructor(transformer = {}) {
      let readableController = null;
      const readable = new ReadableStream({
        start(controller) { readableController = controller; },
      });
      const writable = new WritableStream({
        write(chunk) {
          let output = chunk;
          if (typeof transformer.transform === "function") {
            const outgoing = [];
            const downstream = {
              enqueue(value) { outgoing.push(value); },
              close() {},
              error() {},
            };
            transformer.transform(chunk, downstream);
            for (const value of outgoing) {
              if (readableController) readableController.enqueue(value);
            }
            return;
          }
          if (readableController) readableController.enqueue(output);
        },
      });
      this.readable = readable;
      this.writable = writable;
    }
  }

  // ------------------------------------------------ event constructors
  // UIEvent/Mouse/Pointer/Keyboard/Input/Focus/Wheel/Drag/Clipboard/
  // Touch/TouchList/TouchEvent/Composition/Animation/Transition/
  // Submit/Progress are native host constructors now (G4 ledger
  // chunk 3; see LV8Events.cpp InstallEventFamily).
  // ------------------------------------------------ offline Request wrapper
  // (fetch abort integration: signal-aware fetch over the native bridge)
  var fetchSignalAwareDone = false;
  if (typeof fetch === "function" && !fetchSignalAwareDone) {
    const nativeFetch = fetch;
    const signalAwareFetch = function fetch(input, init = undefined) {
      const signal = init && init.signal;
      if (signal && signal.aborted) return Promise.reject(signal.reason);
      const promise = nativeFetch(input, init);
      if (!signal) return promise;
      let settled = false;
      return new Promise((resolve, reject) => {
        const onAbort = () => {
          if (settled) return;
          settled = true;
          reject(signal.reason);
        };
        signal.addEventListener("abort", onAbort, { once: true });
        promise.then(
          value => { if (!settled) { settled = true; resolve(value); } },
          reason => { if (!settled) { settled = true; reject(reason); } },
        );
      });
    };
    // G4.393l: install a bound twin - a bound function carries no
    // .prototype property (the reference fetch is prototype-less) while
    // keeping the signal-aware behavior; restore the reference name face
    // (a plain bound renders "bound fetch").  The re-entry guard is the
    // closure var (the old function property was an observable own key
    // the reference fetch does not carry).
    fetchSignalAwareDone = true;
    const publicFetch = signalAwareFetch.bind(null);
    try {
      Object.defineProperty(publicFetch, "name",
        {value: "fetch", configurable: true});
    } catch (_) {}
    try {
      Object.defineProperty(globalThis, "fetch", {
        value: publicFetch, writable: true, enumerable: true, configurable: true,
      });
    } catch (_) {}
  }

  // WebMIDI (profile-driven): native since G4 ledger chunk 8 — the JS
  // requestMIDIAccess bridge was removed with it.

  // ------------------------------------------------ headless-probe surface
  // classic old-headless detector signal: navigator.userActivation must
  // exist (Chrome 88+); no user gesture ever occurs in this environment
  try {
    if (typeof navigator !== "undefined" && !navigator.userActivation) {
      const userActivation = {};
      setToStringTag(userActivation, "UserActivation");
      Object.defineProperties(userActivation, {
        hasBeenActive: { value: false, enumerable: true },
        isActive: { value: false, enumerable: true },
      });
      Object.defineProperty(navigator, "userActivation", {
        get: () => userActivation,
        configurable: true,
        enumerable: false,
      });
    }
  } catch (_) {}

  // ------------------------------------------------ logical-clock Date
  // The reference drives Date by the logical clock with a 1ms read step
  // (Date.now() advances the clock bookkeeping per read). Constructing
  // with arguments keeps native behavior; only the zero-arg paths route
  // through the clock.
  const internalBridge = (globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__) || {
    clockMs: globalThis.__lv8_clock_ms, dateRead: globalThis.__lv8_date_read,
    dateStep: globalThis.__lv8_date_step, profile: globalThis.__LV8_PROFILE__
  };
  if (internalBridge && typeof internalBridge.clockMs === "function" &&
      !(internalBridge.profile &&
        internalBridge.profile.$time_mode === "system") &&
      !Date._lv8ClockDriven) {
    const NativeDate = Date;
    const systemOrigin = NativeDate.now();
    let dateReadOffset = 0;
    const clockNow = () => systemOrigin + internalBridge.clockMs();
    const Lv8Date = function (...args) {
      if (!new.target) {
        return new NativeDate(clockNow()).toString();
      }
      return args.length === 0
        ? new NativeDate(clockNow())
        : Reflect.construct(NativeDate, args);
    };
    Lv8Date.prototype = NativeDate.prototype;
    Object.setPrototypeOf(Lv8Date, NativeDate);
    Lv8Date.now = () => {
      // eventLoop date-advance step: each read nudges the logical clock
      // by the step and returns it (no per-call offset while active)
      if (typeof internalBridge.dateStep === "function" && internalBridge.dateStep() > 0 &&
          typeof internalBridge.dateRead === "function") {
        dateReadOffset = 0;
        return Math.floor(systemOrigin + internalBridge.dateRead());
      }
      const value = Math.floor(clockNow() + dateReadOffset);
      dateReadOffset += 1;
      return value;
    };
    Lv8Date.parse = NativeDate.parse;
    Lv8Date.UTC = NativeDate.UTC;
    Object.defineProperty(Lv8Date, "name", { value: "Date" });
    try {
      Object.defineProperty(globalThis, "Date", {
        value: Lv8Date, writable: true, enumerable: false, configurable: true,
      });
      Date._lv8ClockDriven = true;
    } catch (_) {}
  }
  // The reference keeps one monotonic millisecond source for both Date.now()
  // and zero-argument Date construction, even when the surrounding runtime
  // uses wall-clock scheduling.  Native V8 can return the same millisecond
  // repeatedly for back-to-back `new Date()` calls, which leaks into the WAF
  // calibration stream.  Explicit Date arguments remain native values.
  else if (internalBridge && internalBridge.profile &&
           internalBridge.profile.$time_mode === "system" &&
           !Date._lv8SystemClockDriven) {
    const NativeDate = Date;
    let lastDateMs = NativeDate.now();
    const nextDateMs = () => {
      const current = NativeDate.now();
      lastDateMs = current > lastDateMs ? current : lastDateMs + 1;
      return lastDateMs;
    };
    const Lv8SystemDate = function (...args) {
      if (!new.target) return NativeDate();
      return args.length === 0
        ? new NativeDate(nextDateMs())
        : Reflect.construct(NativeDate, args);
    };
    Lv8SystemDate.prototype = NativeDate.prototype;
    Object.setPrototypeOf(Lv8SystemDate, NativeDate);
    Lv8SystemDate.now = () => nextDateMs();
    Lv8SystemDate.parse = NativeDate.parse;
    Lv8SystemDate.UTC = NativeDate.UTC;
    Object.defineProperty(Lv8SystemDate, "name", { value: "Date" });
    try {
      Object.defineProperty(globalThis, "Date", {
        value: Lv8SystemDate, writable: true, enumerable: false,
        configurable: true,
      });
      Date._lv8SystemClockDriven = true;
    } catch (_) {}
  }

  // ------------------------------------------------ installation
  globalThis.__lv8ElementPatches = elementPatches;
  globalThis.__lv8ElementAccessors = elementAccessors;
  globalThis.__lv8ElementAccessorsSetters = {
    innerText: function (newValue) {
      this.textContent = newValue;
    },
    value: function (newValue) {
      if (String(this.tagName || "").toLowerCase() === "input" &&
          String(this.getAttribute("type") || "").toLowerCase() === "file") {
        // file inputs refuse non-empty values and clear on ""
        if (newValue === null || newValue === undefined ||
            String(newValue) === "") {
          this.files = null;
          return;
        }
        throw new Error("InvalidStateError");
      }
      if (newValue === null || newValue === undefined) this.removeAttribute("value");
      else this.setAttribute("value", String(newValue));
    },
  };
  globalThis.__lv8DocumentPatches = documentPatches;
  const globals3 = [
    ["ReadableStream", ReadableStream], ["WritableStream", WritableStream],
    ["TransformStream", TransformStream],
    ["Selection", class Selection {
      constructor() {
        throw new TypeError("Illegal constructor");
      }
    }],
    ["EventTarget", class EventTarget {
      // reference parity (M11): instances are zero-own - the event trio lives on
      // EventTarget.prototype (element_hierarchy pass 4 re-homes the native
      // element methods there); per-instance state would pollute the
      // own-face the WAF enumerates
      constructor() {}
    }],
  ];
  for (const [name, value] of globals3) {
    if (typeof globalThis[name] === "undefined") {
      Object.defineProperty(globalThis, name, {
        value, writable: true, enumerable: true, configurable: true,
      });
    }
  }
})();
"""
