"""lv8 engine prelude: the page-load surface restoration script.

native load_document re-lays prototype surfaces away from the
bootstrap-installed faces.  This script runs after every page load and
before any page script, restoring the reference-measured surfaces:

  - XHR on* handler defaults on an event-target ancestor prototype layer
    (chain-preserving insertion)
  - media element member face (canPlayType/play/pause/paused, the reference
    value members, and the throwing seekable/buffered/controlsList
    getters with reference's captured messages)
  - document instance shape (single own `location` accessor)
  - scrollingElement null (reference wheel behavior)
  - chrome.app.InstallState, external.IsSearchProviderInstalled,
    CDATASection.prototype.remove, mediaCapabilities.decodingInfo
  - matchMedia query overrides ((any-pointer: fine), (any-hover: hover),
    (color-gamut: srgb), (orientation: landscape))
"""

from __future__ import annotations

import json as _json
from .ref_ctor_argreq import REF_CTOR_ARGREQ as _REF_CTOR_ARGREQ
from .ref_ctor_zeroarg import REF_CTOR_ZEROARG as _REF_CTOR_ZEROARG
from .ref_ctor_thin import REF_CTOR_THIN as _REF_CTOR_THIN
from .ref_ctor_thinvalues import REF_CTOR_THINVALUES as _REF_CTOR_THINVALUES
from .ref_interface_chains import INTERFACE_CHAIN_INSTALL_JS
from .ref_interface_chains import WINDOW_NATIVE_FORM_JS

from pathlib import Path
REPO = Path(__file__).resolve().parents[1]


# The cookie-jar expires-parity wrap, also spliced into the binder: the
# SDK's malformed-expires enable_ refresh lands while page scripts load.
COOKIE_JAR_PARITY_JS = "(function(){try{var DC=null,ccp=document;while(ccp&&!DC){DC=Object.getOwnPropertyDescriptor(ccp,'cookie');if(!DC)ccp=Object.getPrototypeOf(ccp);}if(DC&&DC.get&&DC.set){var GMT=/^(Mon|Tue|Wed|Thu|Fri|Sat|Sun),\\s*\\d{1,2}\\s+(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\\s+\\d{4}\\s+\\d{2}:\\d{2}:\\d{2}\\s+GMT$/i;Object.defineProperty(document,'cookie',{get:DC.get,set:function(v){var s=String(v);var m=/;\\s*expires=([^;]*)/i.exec(s);if(m){var tm=/^(\\d{2}):(\\d{2}):(\\d{2})$/.exec(m[1].trim());if(tm){var now=new Date();var exp=new Date(now.getFullYear(),now.getMonth(),now.getDate(),+tm[1],+tm[2],+tm[3]);if(exp.getTime()<=now.getTime()){var name=s.split('=')[0];var dir=('/'+(window.location&&window.location.pathname||'')).replace(/\\/[^\\/]*$/,'')||'/';try{DC.set.call(this,name+'=; Max-Age=0; path=/');}catch(e0){}try{DC.set.call(this,name+'=; Max-Age=0; path='+dir);}catch(e1){}return;}s=s.slice(0,m.index)+'; expires='+exp.toUTCString()+s.slice(m.index+m[0].length);}}return DC.set.call(this,s);},configurable:true,enumerable:true});}}catch(e){}})();"

# Shared with the early document binder: SDK scripts execute during load_document.
ANCHOR_URL_PARITY_JS = "(function(){try{var AP=window.HTMLAnchorElement&&HTMLAnchorElement.prototype;var AHD=AP&&Object.getOwnPropertyDescriptor(AP,'href');if(AP&&AHD&&AHD.get){var __lv8ParseUrl=function(raw,base){var s=String(raw==null?'':raw);var m=/^[a-zA-Z][a-zA-Z0-9+.\\-]*:/.exec(s);if(m){var sch=m[0].slice(0,-1).toLowerCase();var rest=s.slice(m[0].length);if(sch==='about'){return {protocol:'about:',hostname:'',host:'',port:'',pathname:rest,search:'',hash:'',origin:'null'};}var am=/^\\/\\/([^/?#]*)([^#]*)?(#.*)?$/.exec(rest);if(am){var auth=am[1]||'';var pth=am[2]||'';var hsh=am[3]||'';var srch='';var qi=pth.indexOf('?');if(qi>=0){srch=pth.slice(qi);pth=pth.slice(0,qi);}var hp=auth;var at=auth.lastIndexOf('@');if(at>=0)hp=auth.slice(at+1);var host=hp,port='';var ci=hp.lastIndexOf(':');if(ci>hp.lastIndexOf(']')){host=hp.slice(0,ci);port=hp.slice(ci+1);}return {protocol:sch+':',hostname:host,host:port?host+':'+port:host,port:port,pathname:pth||'/',search:srch,hash:hsh,origin:sch+'://'+host+(port?':'+port:'')};}return {protocol:sch+':',hostname:'',host:'',port:'',pathname:rest,search:'',hash:'',origin:'null'};}var bm=/^([a-zA-Z][a-zA-Z0-9+.\\-]*):\\/\\/([^/?#]*)([^#]*)?(#.*)?$/.exec(base||'');if(!bm){return {protocol:'',hostname:'',host:'',port:'',pathname:'',search:'',hash:'',origin:''};}var bsch=bm[1].toLowerCase();var bauth=bm[2];var bpth=bm[3]||'/';var bhsh=bm[4]||'';var bsrch='';var bi2=bpth.indexOf('?');if(bi2>=0){bsrch=bpth.slice(bi2);bpth=bpth.slice(0,bi2);}var dir=bpth.slice(0,bpth.lastIndexOf('/')+1);var full;if(s.charAt(0)==='/')full=s;else if(s.charAt(0)==='?')full=bpth+srch+s;else if(s.charAt(0)==='#')full=bpth+bsrch+s;else full=dir+s;var segs=full.split('/');var out=[];for(var si=0;si<segs.length;si++){var sg=segs[si];if(sg==='..'){if(out.length&&out[out.length-1]!=='')out.pop();}else if(sg==='.'){}else out.push(sg);}full=out.join('/');var h2='';var hi2=full.indexOf('#');if(hi2>=0){h2=full.slice(hi2);full=full.slice(0,hi2);}var s2='';var si2=full.indexOf('?');if(si2>=0){s2=full.slice(si2);full=full.slice(0,si2);}var bhost=bauth,bport='';var ci2=bauth.lastIndexOf(':');if(ci2>bauth.lastIndexOf(']')){bhost=bauth.slice(0,ci2);bport=bauth.slice(ci2+1);}return {protocol:bsch+':',hostname:bhost,host:bport?bhost+':'+bport:bhost,port:bport,pathname:full||'/',search:s2,hash:h2,origin:bsch+'://'+bhost+(bport?':'+bport:'')};};var __lv8HrefMap=new WeakMap();try{Object.defineProperty(AP,'href',{get:function(){var raw=null;try{raw=__lv8HrefMap.get(this);}catch(e){}if(raw===null||raw===undefined){try{raw=AHD.get.call(this);}catch(e2){raw='';}}var p=__lv8ParseUrl(raw,window.location&&window.location.href);if(p.origin==='null'||p.origin==='')return String(raw);return p.protocol+'//'+p.host+p.pathname+p.search+p.hash;},set:function(v){try{__lv8HrefMap.set(this,String(v));}catch(e){}try{AHD.set.call(this,v);}catch(e2){}},configurable:true,enumerable:true});}catch(e){}['protocol','host','hostname','port','pathname','search','hash','origin'].forEach(function(k){try{var getter=function(){var u=this.href;var p=__lv8ParseUrl(u,window.location&&window.location.href);return p[k];};try{Object.defineProperty(getter,'name',{value:'get '+k,configurable:true});}catch(e3){}var setter=null;if(k!=='origin'){setter=function(v){var base=window.location&&window.location.href;var q=__lv8ParseUrl(this.href,base);v=String(v);if(k==='protocol'){q.protocol=v.replace(/:$/,'')+':';}else if(k==='host'){q.host=v;var ci=v.lastIndexOf(':');if(ci>0){q.hostname=v.slice(0,ci);q.port=v.slice(ci+1);}else{q.hostname=v;q.port='';}}else if(k==='hostname'){q.hostname=v;q.host=q.port?q.hostname+':'+q.port:q.hostname;}else if(k==='port'){q.port=v;q.host=v?q.hostname+':'+v:q.hostname;}else if(k==='pathname'){q.pathname=v;}else if(k==='search'){q.search=v===''?'':(v.charAt(0)==='?'?v:'?'+v);}else if(k==='hash'){q.hash=v===''?'':(v.charAt(0)==='#'?v:'#'+v);}this.href=q.protocol+'//'+q.host+(q.pathname||'/')+(q.search||'')+(q.hash||'');};try{Object.defineProperty(setter,'name',{value:'set '+k,configurable:true});}catch(e3s){}}Object.defineProperty(AP,k,{get:getter,set:setter,configurable:true,enumerable:true});}catch(e){}});}}catch(e){}})();"

# The URL parser above keeps an explicitly written default port.  The wheel's
# anchor serialization removes :443 for HTTPS and :80 for HTTP.
ANCHOR_URL_PARITY_JS += r"""(function(){
  try {
    var p = window.HTMLAnchorElement && HTMLAnchorElement.prototype;
    var d = p && Object.getOwnPropertyDescriptor(p, 'href');
    if (!d || !d.get) return;
    function getHref() {
      var s = String(d.get.call(this));
      return s.replace(/^(https:\/\/[^\/?#]+):443(?=\/|[?#]|$)/i, '$1')
        .replace(/^(http:\/\/[^\/?#]+):80(?=\/|[?#]|$)/i, '$1');
    }
    function setHref(v) { if (d.set) d.set.call(this, v); }
    try {
      Object.defineProperty(getHref, 'name', {value: 'get href',
        configurable: true});
      Object.defineProperty(setHref, 'name', {value: 'set href',
        configurable: true});
    } catch (_) {}
    try {
      var w = globalThis.__lv8__ && globalThis.__lv8__.wrapNative;
      if (w) { w(getHref, 'get href'); w(setHref, 'set href'); }
    } catch (_) {}
    Object.defineProperty(p, 'href', {get: getHref, set: setHref,
      enumerable: true, configurable: true});
  } catch (_) {}
})();"""

__all__ = ["PAGE_LOAD_PRELUDE"]

# reference's for-in(document) on* walk order (GlobalEventHandlers mixin,
# Chrome spec order) - engine-visible through the challenge SDK's
# environment scan (work/reference-traces/document-forin-on-order.json)
_DOCUMENT_ON_WALK = (
    "onreadystatechange",
    "onpointerlockchange",
    "onpointerlockerror",
    "onbeforecopy",
    "onbeforecut",
    "onbeforepaste",
    "onfreeze",
    "onprerenderingchange",
    "onresume",
    "onsearch",
    "onvisibilitychange",
    "onscrollend",
    "ontouchstart",
    "ontouchmove",
    "ontouchend",
    "ontouchcancel",
    "onfullscreenchange",
    "onfullscreenerror",
    "onwebkitfullscreenchange",
    "onwebkitfullscreenerror",
    "onbeforexrselect",
    "onabort",
    "onbeforeinput",
    "onbeforematch",
    "onbeforetoggle",
    "onblur",
    "oncancel",
    "oncanplay",
    "oncanplaythrough",
    "onchange",
    "onclick",
    "onclose",
    "oncontextlost",
    "oncontextmenu",
    "oncontextrestored",
    "oncuechange",
    "ondblclick",
    "ondrag",
    "ondragend",
    "ondragenter",
    "ondragleave",
    "ondragover",
    "ondragstart",
    "ondrop",
    "ondurationchange",
    "onemptied",
    "onended",
    "onerror",
    "onfocus",
    "onformdata",
    "oninput",
    "oninvalid",
    "onkeydown",
    "onkeypress",
    "onkeyup",
    "onload",
    "onloadeddata",
    "onloadedmetadata",
    "onloadstart",
    "onmousedown",
    "onmouseenter",
    "onmouseleave",
    "onmousemove",
    "onmouseout",
    "onmouseover",
    "onmouseup",
    "onmousewheel",
    "onpause",
    "onplay",
    "onplaying",
    "onprogress",
    "onratechange",
    "onreset",
    "onresize",
    "onscroll",
    "onsecuritypolicyviolation",
    "onseeked",
    "onseeking",
    "onselect",
    "onslotchange",
    "onstalled",
    "onsubmit",
    "onsuspend",
    "ontimeupdate",
    "ontoggle",
    "onvolumechange",
    "onwaiting",
    "onwebkitanimationend",
    "onwebkitanimationiteration",
    "onwebkitanimationstart",
    "onwebkittransitionend",
    "onwheel",
    "onauxclick",
    "ongotpointercapture",
    "onlostpointercapture",
    "onpointerdown",
    "onpointermove",
    "onpointerrawupdate",
    "onpointerup",
    "onpointercancel",
    "onpointerover",
    "onpointerout",
    "onpointerenter",
    "onpointerleave",
    "onselectstart",
    "onselectionchange",
    "onanimationend",
    "onanimationiteration",
    "onanimationstart",
    "ontransitionrun",
    "ontransitionstart",
    "ontransitionend",
    "ontransitioncancel",
    "oncopy",
    "oncut",
    "onpaste",
)
pipe = '|'.join(_DOCUMENT_ON_WALK)

# form.elements: the page business core's detached-form fingerprint stage
# reads form.elements on a programmatic (never-attached) form before
# anything else.  lv8's HTMLFormElement face lacked `elements`, the stage
# bailed, its font-enumeration block never ran, and the page-signature signature
# diverged - the WAF 400-blocked every lv8 signed POST (G4.221).  Mirrors
# the reference wheel's collection face exactly: length, named-by-name/id,
# namedItem, HTMLFormControlsCollection toStringTag, NO indexed own access
# (reference reads el[0] as undefined too).  Installed by BOTH preludes so the
# accessor exists for the first load's async stages (PAGE_LOAD_PRELUDE,
# after load_document exposed the ctors) and for later loads' sync stages
# (REALM_RESET_PRELUDE, before their scripts run).
FORM_ELEMENTS_INSTALL = """
(function () {
  try {
    if (typeof HTMLFormElement === 'undefined') return;
    if (Object.prototype.hasOwnProperty.call(
        HTMLFormElement.prototype, 'elements')) return;
    var CONTROL = {INPUT: 1, SELECT: 1, TEXTAREA: 1, BUTTON: 1,
                   FIELDSET: 1, OBJECT: 1, OUTPUT: 1};
    function collect(form) {
      var out = [];
      (function walk(node) {
        var kids = node.children;
        for (var i = 0; i < kids.length; i++) {
          var k = kids[i];
          if (CONTROL[k.tagName]) out.push(k);
          walk(k);
        }
      })(form);
      return out;
    }
    Object.defineProperty(HTMLFormElement.prototype, 'elements', {
      get: function () {
        var controls = collect(this);
        var col = {};
        for (var i = 0; i < controls.length; i++) {
          var c = controls[i];
          var name = c.getAttribute && c.getAttribute('name');
          if (name && !(name in col)) col[name] = c;
          var id = c.getAttribute && c.getAttribute('id');
          if (id && !(id in col)) col[id] = c;
        }
        col.namedItem = function (name) {
          for (var j = 0; j < controls.length; j++) {
            var c = controls[j];
            if ((c.getAttribute && c.getAttribute('name')) === name ||
                (c.getAttribute && c.getAttribute('id')) === name) return c;
          }
          return null;
        };
        col.length = controls.length;
        Object.defineProperty(col, Symbol.toStringTag, {
          value: 'HTMLFormControlsCollection', configurable: true});
        return col;
      },
      configurable: true,
      enumerable: true
    });
  } catch (e) {}
})();
"""

# HTMLInputElement IDL defaults (G4.222 read-order A/B): the d8y2
# storage gate compares input.type against candidate strings; lv8's
# reflected stubs returned ""/0 where the reference returns "text"/20/-1,
# which sent the SDK down the fingerprint-skip path.  fixElementHierarchy
# re-lays element prototypes on every load, so this must run inside both
# preludes (after the fix), not at boot.
# HTMLFormElement IDL face (G4.222): the d8y2 form probe reads
# method/enctype/encoding/target/acceptCharset/autocomplete/noValidate/
# action off a synthetic detached form.  reference returns the IDL reflection
# defaults (method 'get', enctype/encoding
# 'application/x-www-form-urlencoded', target '_self', acceptCharset
# 'UTF-8', autocomplete 'on', noValidate false) and an EMPTY action for
# an absent attribute; lv8 returned undefined / the resolved document
# URL, which sent the form-fingerprint stage selection into the media
# fallback.  checkValidity/reportValidity return true.
# Function-name sweep (G4.222): every host-prototype function member on
# the reference carries its member name ('method', 'get method', 'set
# method'); lv8's generated face left hundreds unnamed (Element.prototype
# alone had 204).  The d8y2 state machine hashes function names.
FACE_PARITY_INSTALL = """
(function () {
  // ---- 1. CSSStyleDeclaration: clean chain + declaration members ----
  try {
    var csd = window.CSSStyleDeclaration;
    if (csd && csd.prototype) {
      var cp = csd.prototype;
      if (Object.getPrototypeOf(cp) !== Object.prototype) {
        try { Object.setPrototypeOf(cp, Object.prototype); } catch (e0) {}
      }
      // reference for-in over a style instance sees exactly 9 declaration-level
      // members (cssText/length/parentRule/cssFloat/getPropertyPriority/
      // getPropertyValue/item/removeProperty/setProperty) - the own
      // constructor is NOT enumerable there.
      try {
        var cd = Object.getOwnPropertyDescriptor(cp, 'constructor');
        if (cd && cd.enumerable) {
          cd.enumerable = false;
          Object.defineProperty(cp, 'constructor', cd);
        }
      } catch (eR) {}
      function ensureAccessor(obj, key, get, set) {
        if (!Object.prototype.hasOwnProperty.call(obj, key)) {
          try {
            Object.defineProperty(obj, key, {
              get: get, set: set, enumerable: true, configurable: true });
          } catch (e1) {}
        }
      }
      ensureAccessor(cp, 'parentRule',
        function () { return null; }, function () {});
      ensureAccessor(cp, 'cssFloat',
        function () { return this.getPropertyValue('float') || ''; },
        function (v) { try { this.setProperty('float', v); } catch (e2) {} });
      if (!Object.prototype.hasOwnProperty.call(cp, 'getPropertyPriority')) {
        try {
          Object.defineProperty(cp, 'getPropertyPriority', {
            value: function () { return ''; },
            writable: true, enumerable: true, configurable: true });
        } catch (e4) {}
      }
      // The 347 reference-measured supported camelCase names as proto accessors:
      // the native inline-style view answers reads but not `in`, so the
      // collector's existence checks need real members on the chain.
      // Instance-level storage (set props / native interceptors) always
      // wins over the proto, so reflection behavior is unchanged.
      if (!cp.__lv8CssInNames) {
        try {
          var cssIn = {};
          ("accentColor,alignContent,alignItems,alignSelf,alignmentBaseline,all,anchorName,animationComposition,animationDelay,animationDirection,animationDuration,animationFillMode,animationIterationCount,animationName,animationPlayState,animationRangeEnd,animationRangeStart,animationTimeline,animationTimingFunction,appRegion,appearance,backdropFilter,backfaceVisibility,backgroundAttachment,backgroundBlendMode,backgroundClip,backgroundColor,backgroundImage,backgroundOrigin,backgroundPosition,backgroundRepeat,backgroundSize,baselineShift,baselineSource,blockSize,borderBlockEndColor,borderBlockEndStyle,borderBlockEndWidth,borderBlockStartColor,borderBlockStartStyle,borderBlockStartWidth,borderBottomColor,borderBottomLeftRadius,borderBottomRightRadius,borderBottomStyle,borderBottomWidth,borderCollapse,borderEndEndRadius,borderEndStartRadius,borderImageOutset,borderImageRepeat,borderImageSlice,borderImageSource,borderImageWidth,borderInlineEndColor,borderInlineEndStyle,borderInlineEndWidth,borderInlineStartColor,borderInlineStartStyle,borderInlineStartWidth,borderLeftColor,borderLeftStyle,borderLeftWidth,borderRightColor,borderRightStyle,borderRightWidth,borderStartEndRadius,borderStartStartRadius,borderTopColor,borderTopLeftRadius,borderTopRightRadius,borderTopStyle,borderTopWidth,bottom,boxShadow,boxSizing,breakAfter,breakBefore,breakInside,bufferedRendering,captionSide,caretColor,clear,clip,clipPath,clipRule,color,colorInterpolation,colorInterpolationFilters,colorRendering,colorScheme,columnCount,columnGap,columnRuleColor,columnRuleStyle,columnRuleWidth,columnSpan,columnWidth,containIntrinsicBlockSize,containIntrinsicHeight,containIntrinsicInlineSize,containIntrinsicSize,containIntrinsicWidth,containerName,containerType,content,counterIncrement,counterSet,cssFloat,cssText,cursor,cx,cy,d,direction,display,dominantBaseline,emptyCells,fieldSizing,fill,fillOpacity,fillRule,filter,flexBasis,flexDirection,flexGrow,flexShrink,flexWrap,float,floodColor,floodOpacity,fontFamily,fontKerning,fontOpticalSizing,fontPalette,fontSize,fontStretch,fontStyle,fontSynthesisSmallCaps,fontSynthesisStyle,fontSynthesisWeight,fontVariant,fontVariantAlternates,fontVariantCaps,fontVariantEastAsian,fontVariantLigatures,fontVariantNumeric,fontVariantPosition,fontWeight,gridAutoColumns,gridAutoFlow,gridAutoRows,gridColumnEnd,gridColumnStart,gridRowEnd,gridRowStart,gridTemplateAreas,gridTemplateColumns,gridTemplateRows,height,hyphenateCharacter,hyphenateLimitChars,hyphens,imageOrientation,imageRendering,initialLetter,inlineSize,insetArea,insetBlockEnd,insetBlockStart,insetInlineEnd,insetInlineStart,isolation,justifyContent,justifyItems,justifySelf,left,length,letterSpacing,lightingColor,lineBreak,lineHeight,listStyleImage,listStylePosition,listStyleType,marginBlockEnd,marginBlockStart,marginBottom,marginInlineEnd,marginInlineStart,marginLeft,marginRight,marginTop,markerEnd,markerMid,markerStart,maskClip,maskComposite,maskImage,maskMode,maskOrigin,maskPosition,maskRepeat,maskSize,maskType,mathDepth,mathShift,mathStyle,maxBlockSize,maxHeight,maxInlineSize,maxWidth,minBlockSize,minHeight,minInlineSize,minWidth,mixBlendMode,objectFit,objectPosition,objectViewBox,offsetAnchor,offsetDistance,offsetPath,offsetPosition,offsetRotate,opacity,order,orphans,outlineColor,outlineOffset,outlineStyle,outlineWidth,overflowAnchor,overflowClipMargin,overflowWrap,overflowX,overflowY,overlay,overscrollBehaviorBlock,overscrollBehaviorInline,paddingBlockEnd,paddingBlockStart,paddingBottom,paddingInlineEnd,paddingInlineStart,paddingLeft,paddingRight,paddingTop,paintOrder,parentRule,perspective,perspectiveOrigin,pointerEvents,position,positionAnchor,positionTry,positionTryOptions,positionTryOrder,positionVisibility,r,resize,right,rotate,rowGap,rubyPosition,rx,ry,scale,scrollBehavior,scrollMarginBlockEnd,scrollMarginBlockStart,scrollMarginInlineEnd,scrollMarginInlineStart,scrollPaddingBlockEnd,scrollPaddingBlockStart,scrollPaddingInlineEnd,scrollPaddingInlineStart,scrollTimelineAxis,scrollTimelineName,scrollbarColor,scrollbarGutter,scrollbarWidth,shapeImageThreshold,shapeMargin,shapeOutside,shapeRendering,speak,stopColor,stopOpacity,stroke,strokeDasharray,strokeDashoffset,strokeLinecap,strokeLinejoin,strokeMiterlimit,strokeOpacity,strokeWidth,tabSize,tableLayout,textAlign,textAlignLast,textAnchor,textDecoration,textDecorationColor,textDecorationLine,textDecorationSkipInk,textDecorationStyle,textEmphasisColor,textEmphasisPosition,textEmphasisStyle,textIndent,textOverflow,textRendering,textShadow,textSizeAdjust,textSpacingTrim,textTransform,textUnderlinePosition,textWrap,timelineScope,top,touchAction,transform,transformOrigin,transformStyle,transitionBehavior,transitionDelay,transitionDuration,transitionProperty,transitionTimingFunction,translate,unicodeBidi,userSelect,vectorEffect,verticalAlign,viewTimelineAxis,viewTimelineInset,viewTimelineName,viewTransitionClass,viewTransitionName,visibility,webkitAlignContent,webkitAlignItems,webkitAlignSelf,webkitAnimation,webkitAnimationDelay,webkitAnimationDirection,webkitAnimationDuration,webkitAnimationFillMode,webkitAnimationIterationCount,webkitAnimationName,webkitAnimationPlayState,webkitAnimationTimingFunction,webkitAppRegion,webkitAppearance,webkitBackfaceVisibility,webkitBackgroundClip,webkitBackgroundOrigin,webkitBackgroundSize,webkitBorderBottomLeftRadius,webkitBorderBottomRightRadius,webkitBorderRadius,webkitBorderTopLeftRadius,webkitBorderTopRightRadius,webkitBoxDirection,webkitBoxOrient,webkitBoxShadow,webkitBoxSizing,webkitClipPath,webkitColumnCount,webkitColumnGap,webkitColumnRuleColor,webkitColumnRuleStyle,webkitColumnRuleWidth,webkitColumnSpan,webkitColumnWidth,webkitFilter,webkitFlexBasis,webkitFlexDirection,webkitFlexGrow,webkitFlexShrink,webkitFlexWrap,webkitHyphenateCharacter,webkitJustifyContent,webkitLineBreak,webkitLineClamp,webkitMaskClip,webkitMaskComposite,webkitMaskImage,webkitMaskOrigin,webkitMaskPosition,webkitMaskRepeat,webkitMaskSize,webkitOpacity,webkitOrder,webkitPerspective,webkitPerspectiveOrigin,webkitRubyPosition,webkitShapeImageThreshold,webkitShapeMargin,webkitShapeOutside,webkitTapHighlightColor,webkitTextEmphasisColor,webkitTextEmphasisPosition,webkitTextEmphasisStyle,webkitTextSizeAdjust,webkitTextStroke,webkitTransform,webkitTransformOrigin,webkitTransformStyle,webkitTransition,webkitTransitionDelay,webkitTransitionDuration,webkitTransitionProperty,webkitTransitionTimingFunction,webkitUserSelect,webkitWritingMode,whiteSpaceCollapse,widows,width,willChange,wordBreak,wordSpacing,writingMode,x,y,zIndex,zoom").split(",").forEach(function (n) {
            cssIn[n] = 1; });
          Object.keys(cssIn).forEach(function (name) {
            if (Object.prototype.hasOwnProperty.call(cp, name)) return;
            try {
              Object.defineProperty(cp, name, {
                get: function () {
                  if (typeof this.getPropertyValue === 'function') {
                    try {
                      var kebab = name.replace(/[A-Z]/g,
                        function (c) { return '-' + c.toLowerCase(); });
                      return this.getPropertyValue(kebab) || '';
                    } catch (e20) {}
                  }
                  return '';
                },
                set: function (v) {
                  if (typeof this.setProperty === 'function') {
                    try {
                      var kebab2 = name.replace(/[A-Z]/g,
                        function (c) { return '-' + c.toLowerCase(); });
                      this.setProperty(kebab2, v);
                    } catch (e21) {}
                  }
                },
                // reference for-in over a style instance enumerates only the 9
                // declaration-level members - the property interceptors
                // are NOT enumerable; mirror that here.
                enumerable: false, configurable: true });
            } catch (e22) {}
          });
          try { cp.__lv8CssInNames = true; } catch (e23) {}
        } catch (e24) {}
      }
    }
  } catch (e) {}

  // ---- 3. HTMLBodyElement: WindowEventHandlers on* + legacy attrs ----
  try {
    if (typeof HTMLBodyElement === 'function' && HTMLBodyElement.prototype) {
      var bp = HTMLBodyElement.prototype;
      var onNames = ['onafterprint', 'onbeforeprint', 'onbeforeunload',
        'onhashchange', 'onlanguagechange', 'onmessage', 'onmessageerror',
        'onoffline', 'ononline', 'onpagehide', 'onpageshow', 'onpopstate',
        'onrejectionhandled', 'onstorage', 'onunhandledrejection',
        'onunload'];
      var handlers = new WeakMap();
      onNames.forEach(function (name) {
        if (Object.prototype.hasOwnProperty.call(bp, name)) return;
        try {
          Object.defineProperty(bp, name, {
            get: function () {
              var m = handlers.get(this);
              return m && m[name] !== undefined ? m[name] : null;
            },
            set: function (v) {
              var m = handlers.get(this);
              if (!m) { m = {}; try { handlers.set(this, m); } catch (e5) {} }
              m[name] = v;
            },
            enumerable: true, configurable: true });
        } catch (e6) {}
      });
      function reflected(attr) {
        return {
          get: function () {
            var v = null;
            try { v = this.getAttribute(attr); } catch (e7) {}
            return v === null || v === undefined ? '' : v;
          },
          set: function (val) {
            try { this.setAttribute(attr, String(val)); } catch (e8) {}
          }
        };
      }
      var attrs = { text: 'text', bgColor: 'bgcolor', aLink: 'alink',
                    link: 'link', vLink: 'vlink', background: 'background' };
      Object.keys(attrs).forEach(function (key) {
        if (Object.prototype.hasOwnProperty.call(bp, key)) return;
        var pair = reflected(attrs[key]);
        try {
          Object.defineProperty(bp, key, {
            get: pair.get, set: pair.set,
            enumerable: true, configurable: true });
        } catch (e9) {}
      });
    }
  } catch (e) {}

  // ---- 2. document: Node link + Document/HTMLDocument face ----
  try {
    var doc = window.document;
    if (doc) {
      // lv8's document chain is doc -> dp -> EventTarget.prototype (the
      // engine's Node global is a constants object, not a constructor, so
      // there is no Node.prototype to splice in).  The reference chain is
      // doc -> HTMLDocument.prototype -> Document.prototype ->
      // Node.prototype -> EventTarget.prototype; install the Node-level
      // members directly on the document proto level below.
      var dp2 = (function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();
      while (dp2 && Object.getPrototypeOf(dp2) !== EventTarget.prototype &&
             Object.getPrototypeOf(dp2) !== Object.prototype) {
        dp2 = Object.getPrototypeOf(dp2);
      }
      // Pollution guard: an early realm state can expose the shared Node
      // hierarchy level as the document's proto; installing the Document
      // face there would leak it to every element (`clear` in body etc.).
      // Strip anything we planted on the Node level and only dress the
      // real document proto (which lacks the Node-level constants).
      try {
        var nl = Object.getPrototypeOf((document.body ||
          document.documentElement) || {});
        while (nl && nl.constructor && nl.constructor.name !== 'Node')
          nl = Object.getPrototypeOf(nl);
        if (nl && nl !== dp2) {
          var docOnly = ['clear', 'adoptNode', 'importNode', 'captureEvents',
            'releaseEvents', 'execCommand', 'getElementsByName',
            'createNodeIterator', 'createTreeWalker', 'elementFromPoint',
            'elementsFromPoint', 'caretPositionFromPoint',
            'caretRangeFromPoint', 'createAttribute', 'createAttributeNS',
            'createCDATASection', 'createComment', 'createNSResolver',
            'createProcessingInstruction', 'exitFullscreen',
            'exitPictureInPicture', 'exitPointerLock', 'queryCommandEnabled',
            'queryCommandIndeterm', 'queryCommandState',
            'queryCommandSupported', 'queryCommandValue',
            'getElementsByTagNameNS', 'prepend', 'append', 'moveBefore',
            'replaceChildren', 'bgColor', 'alinkColor', 'fgColor',
            'linkColor', 'vlinkColor', 'fullscreen',
            'pictureInPictureEnabled', 'webkitFullscreenEnabled',
            'webkitHidden', 'webkitIsFullScreen', 'xmlStandalone',
            'xmlVersion', 'fragmentDirective', 'rootElement',
            'webkitCurrentFullScreenElement', 'webkitFullscreenElement',
            'webkitVisibilityState'];
          docOnly.forEach(function (name) {
            if (Object.prototype.hasOwnProperty.call(nl, name)) {
              try { delete nl[name]; } catch (e31) {}
            }
          });
        }
      } catch (e32) {}
      if (dp2 && !Object.prototype.hasOwnProperty.call(dp2, 'ATTRIBUTE_NODE')) {
        function stubFn(name, length) {
          var f = function () { return null; };
          try { Object.defineProperty(f, 'name', { value: name }); } catch (e11) {}
          try { Object.defineProperty(f, 'length', { value: length }); } catch (e12) {}
          return f;
        }
        var fns = {
          adoptNode: 1, importNode: 1, clear: 0, captureEvents: 0,
          releaseEvents: 0, execCommand: 1, getElementsByName: 1,
          createNodeIterator: 1, createTreeWalker: 1, elementFromPoint: 0,
          elementsFromPoint: 2, caretPositionFromPoint: 2,
          caretRangeFromPoint: 2, createAttribute: 1, createAttributeNS: 2,
          createCDATASection: 1, createComment: 1, createNSResolver: 1,
          createProcessingInstruction: 2, exitFullscreen: 0,
          exitPictureInPicture: 0, exitPointerLock: 0,
          queryCommandEnabled: 1, queryCommandIndeterm: 1,
          queryCommandState: 1, queryCommandSupported: 1,
          queryCommandValue: 1, getElementsByTagNameNS: 2, prepend: 0,
          append: 0, moveBefore: 2, replaceChildren: 0
        };
        Object.keys(fns).forEach(function (name) {
          if (!Object.prototype.hasOwnProperty.call(dp2, name) &&
              !(name in dp2)) {
            try {
              Object.defineProperty(dp2, name, {
                value: stubFn(name, fns[name]),
                writable: true, enumerable: true, configurable: true });
            } catch (e13) {}
          }
        });
        // Node-level members (reference carries them via Node.prototype; the
        // reference document answers `in` for all of them)
        var nodeFns = {
          hasChildNodes: 0, appendChild: 1, insertBefore: 2,
          removeChild: 1, replaceChild: 2, cloneNode: 1, normalize: 0,
          contains: 1, compareDocumentPosition: 1, getRootNode: 0,
          isDefaultNamespace: 1, isEqualNode: 1, isSameNode: 1,
          lookupNamespaceURI: 1, lookupPrefix: 1
        };
        // G4.235: reference answers these through Node.prototype's NATIVE methods;
        // the business (second-load) document proto loses the Node link, and
        // the old fallback installed `function () { return null; }` stubs.
        // The SDK grabs document.appendChild into its VM table at boot and
        // later calls it; the stub's null return flipped the f21 state slot
        // (lv8 fed id 8 where reference fed 88 -> ESS fields 50/51 off by 1 bit).
        // Prefer the native implementation (Element/Node prototype) and only
        // fall back to the stub when no host implementation exists.
        var nativeNodeSrc = null;
        try {
          if (typeof Element === 'function' && Element.prototype)
            nativeNodeSrc = Element.prototype;
        } catch (e24a) {}
        if (!nativeNodeSrc) {
          try {
            if (typeof Node === 'function' && Node.prototype)
              nativeNodeSrc = Node.prototype;
          } catch (e24b) {}
        }
        Object.keys(nodeFns).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          var impl = null;
          try { impl = nativeNodeSrc ? nativeNodeSrc[name] : null; } catch (e24c) {}
          if (typeof impl !== 'function') impl = stubFn(name, nodeFns[name]);
          try {
            Object.defineProperty(dp2, name, {
              value: impl,
              writable: true, enumerable: true, configurable: true });
          } catch (e25) {}
        });
        var docNodeVals = { nodeType: 9, nodeName: '#document',
                            parentNode: null, parentElement: null,
                            ownerDocument: null, nextSibling: null,
                            previousSibling: null, isConnected: true,
                            textContent: null, baseURI: null };
        Object.keys(docNodeVals).forEach(function (key) {
          if (Object.prototype.hasOwnProperty.call(dp2, key) ||
              (key in dp2)) return;
          try {
            Object.defineProperty(dp2, key, {
              get: function () {
                if (key === 'baseURI') {
                  try { return String(document.location.href); }
                  catch (e26) { return 'about:blank'; }
                }
                if (key === 'isConnected') return true;
                return docNodeVals[key];
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e27) {}
        });
        // childNodes/firstChild/lastChild over the document element
        if (!Object.prototype.hasOwnProperty.call(dp2, 'childNodes') &&
            !('childNodes' in dp2)) {
          try {
            Object.defineProperty(dp2, 'childNodes', {
              get: function () {
                var out = [];
                try {
                  var kids = this.children;
                  if (kids && kids.length) {
                    for (var i = 0; i < kids.length; i++) out.push(kids[i]);
                  }
                } catch (e28) {}
                return out;
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e29) {}
        }
        ['firstChild', 'lastChild'].forEach(function (key) {
          if (Object.prototype.hasOwnProperty.call(dp2, key) ||
              (key in dp2)) return;
          try {
            Object.defineProperty(dp2, key, {
              get: function () {
                var kids = this.childNodes;
                if (!kids || !kids.length) return null;
                return key === 'firstChild' ? kids[0] : kids[kids.length - 1];
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e30) {}
        });
        function docColor(attr) {
          return {
            get: function () {
              var b = this.body || null;
              var v = null;
              try { v = b && b.getAttribute(attr); } catch (e14) {}
              return v === null || v === undefined ? '' : v;
            },
            set: function (val) {
              try {
                if (this.body) this.body.setAttribute(attr, String(val));
              } catch (e15) {}
            }
          };
        }
        var docAttrs = { bgColor: 'bgcolor', alinkColor: 'alink',
                         fgColor: 'fgcolor', linkColor: 'link',
                         vlinkColor: 'vlink' };
        Object.keys(docAttrs).forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key)) {
            var pair = docColor(docAttrs[key]);
            try {
              Object.defineProperty(dp2, key, {
                get: pair.get, set: pair.set,
                enumerable: true, configurable: true });
            } catch (e16) {}
          }
        });
        var docVals = { fullscreen: false, pictureInPictureEnabled: true,
                        webkitFullscreenEnabled: false,
                        webkitHidden: false, webkitIsFullScreen: false,
                        xmlStandalone: false, xmlVersion: '1.0' };
        Object.keys(docVals).forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key) &&
              !(key in dp2)) {
            try {
              Object.defineProperty(dp2, key, {
                get: function () { return docVals[key]; },
                set: function () {},
                enumerable: true, configurable: true });
            } catch (e17) {}
          }
        });
        var docNulls = ['fragmentDirective', 'rootElement',
                        'webkitCurrentFullScreenElement',
                        'webkitFullscreenElement'];
        docNulls.forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key) &&
              !(key in dp2)) {
            try {
              Object.defineProperty(dp2, key, {
                get: function () { return null; },
                set: function () {},
                enumerable: true, configurable: true });
            } catch (e18) {}
          }
        });
        if (!Object.prototype.hasOwnProperty.call(dp2, 'webkitVisibilityState') &&
            !('webkitVisibilityState' in dp2)) {
          try {
            Object.defineProperty(dp2, 'webkitVisibilityState', {
              get: function () { return 'visible'; },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e19) {}
        }
        // reference-only document members (for-in visible, enumerable)
        // G4.364: createExpression/evaluate left out - the return-null
        // stubs blocked the working XPath install (D1 fork root); the
        // xpath-family pass defines the real members on every document
        // chain instead.
        var docMoreFns = { createEvent: 1, open: 0, close: 0, hasFocus: 0 };
        Object.keys(docMoreFns).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          try {
            Object.defineProperty(dp2, name, {
              value: stubFn(name, docMoreFns[name]),
              writable: true, enumerable: true, configurable: true });
          } catch (e33) {}
        });
        if (!Object.prototype.hasOwnProperty.call(dp2, 'nodeValue') &&
            !('nodeValue' in dp2)) {
          try {
            Object.defineProperty(dp2, 'nodeValue', {
              get: function () { return null; },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e34) {}
        }
        var nodeConsts = { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2,
          TEXT_NODE: 3, CDATA_SECTION_NODE: 4, ENTITY_REFERENCE_NODE: 5,
          ENTITY_NODE: 6, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8,
          DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10,
          DOCUMENT_FRAGMENT_NODE: 11, NOTATION_NODE: 12,
          DOCUMENT_POSITION_DISCONNECTED: 1,
          DOCUMENT_POSITION_PRECEDING: 2,
          DOCUMENT_POSITION_FOLLOWING: 4,
          DOCUMENT_POSITION_CONTAINS: 8,
          DOCUMENT_POSITION_CONTAINED_BY: 16,
          DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 };
        Object.keys(nodeConsts).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          try {
            Object.defineProperty(dp2, name, {
              value: nodeConsts[name],
              writable: false, enumerable: true, configurable: true });
          } catch (e35) {}
        });
        // referrer belongs to the Document interface level, not to the
        // document instance.  The per-load chain split below retains its
        // global-interface descriptor on Document.prototype.
      }
    }
  } catch (e) {}

  // ---- 4. enumerability/count parity (console/performance/screen/ ----
  // ---- location/documentElement), measured against the reference    ----
  try {
    function stubOf(name, length) {
      var f = function () { return undefined; };
      try { Object.defineProperty(f, 'name', { value: name }); } catch (eA) {}
      try { Object.defineProperty(f, 'length', { value: length }); } catch (eB) {}
      return f;
    }
    // console: reference carries 23 own enumerable members in this order
    if (typeof console === 'object' && console) {
      var conNames = ['debug', 'error', 'info', 'log', 'warn', 'dir',
        'dirxml', 'table', 'trace', 'group', 'groupCollapsed', 'groupEnd',
        'clear', 'count', 'countReset', 'assert', 'profile', 'profileEnd',
        'time', 'timeLog', 'timeEnd', 'timeStamp', 'context'];
      conNames.forEach(function (name) {
        if (Object.prototype.hasOwnProperty.call(console, name)) return;
        var cur = console[name];
        var fn = (typeof cur === 'function') ? cur : stubOf(name, 0);
        try {
          Object.defineProperty(console, name, {
            value: fn, writable: true, enumerable: true, configurable: true });
        } catch (eC) {}
      });
    }
    // performance: reference enumerates 20 own members in this order
    try {
      var perfNames = ['timeOrigin', 'onresourcetimingbufferfull',
        'clearMarks', 'clearMeasures', 'clearResourceTimings', 'getEntries',
        'getEntriesByName', 'getEntriesByType', 'mark', 'measure', 'now',
        'setResourceTimingBufferSize', 'toJSON', 'timing', 'navigation',
        'memory', 'eventCounts', 'addEventListener', 'removeEventListener',
        'dispatchEvent'];
      var perf = window.performance;
      if (perf) {
        var saved = {};
        perfNames.forEach(function (name) {
          try { saved[name] = perf[name]; } catch (eD) {}
        });
        perfNames.forEach(function (name) {
          try { delete perf[name]; } catch (eE) {}
          var v = saved[name];
          try {
            Object.defineProperty(perf, name, {
              value: v === undefined ? stubOf(name, 0) : v,
              writable: true, enumerable: true, configurable: true });
          } catch (eF) {}
        });
      }
    } catch (eG) {}
    // screen: reference exposes the EventTarget trio enumerable
    try {
      var sp = window.Screen && Screen.prototype;
      if (sp && typeof EventTarget === 'function' && EventTarget.prototype) {
        ['addEventListener', 'removeEventListener', 'dispatchEvent']
          .forEach(function (name) {
            if (name in sp) return;
            var d = Object.getOwnPropertyDescriptor(EventTarget.prototype,
              name);
            if (d && d.value) {
              try {
                Object.defineProperty(sp, name, { value: d.value,
                  writable: true, enumerable: true, configurable: true });
              } catch (eH) {}
            }
          });
      }
    } catch (eI) {}
    // location: reference owns valueOf FIRST in insertion order
    try {
      var loc = window.location;
      var order = ['valueOf', 'ancestorOrigins', 'href', 'origin', 'protocol',
        'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'assign',
        'reload', 'replace', 'toString'];
      var descs = {};
      order.forEach(function (name) {
        var d = Object.getOwnPropertyDescriptor(loc, name);
        if (d) descs[name] = d;
      });
      if (descs.valueOf) {
        order.forEach(function (name) {
          var d = descs[name];
          if (!d) return;
          try { delete loc[name]; } catch (eJ) {}
        });
        order.forEach(function (name) {
          var d = descs[name];
          if (!d) return;
          try { Object.defineProperty(loc, name, d); } catch (eK) {}
        });
      }
    } catch (eL) {}
    // documentElement: reference has NO own members; drop the mutation
    // wrapper leftovers (the prototype face serves the same operations)
    try {
      var de = document.documentElement;
      if (de) {
        ['__lv8MutationWrapped', 'setAttribute', 'removeAttribute',
         'appendChild', 'removeChild'].forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(de, name)) {
            try { delete de[name]; } catch (eM) {}
          }
        });
      }
    } catch (eN) {}
    // NetworkInformation: reference chains its prototype into EventTarget
    // (navigator.connection inherits addEventListener & co); lv8's stops
    // at Object - the d8y2 collector walks navigator.connection.
    try {
      var nip = window.NetworkInformation && NetworkInformation.prototype;
      if (nip && typeof EventTarget === 'function' &&
          Object.getPrototypeOf(nip) !== EventTarget.prototype &&
          Object.getPrototypeOf(Object.getPrototypeOf(nip)) !==
            EventTarget.prototype) {
        try { Object.setPrototypeOf(nip, EventTarget.prototype); }
        catch (eR2) {}
      }
    } catch (eS2) {}
    // html element: legacy version reflected attribute (reference for-in has it)
    try {
      if (typeof HTMLHtmlElement === 'function' && HTMLHtmlElement.prototype &&
          !Object.prototype.hasOwnProperty.call(HTMLHtmlElement.prototype,
            'version')) {
        Object.defineProperty(HTMLHtmlElement.prototype, 'version', {
          get: function () {
            var v = null;
            try { v = this.getAttribute('version'); } catch (eO) {}
            return v === null || v === undefined ? '' : v;
          },
          set: function (val) {
            try { this.setAttribute('version', String(val)); } catch (eP) {}
          },
          enumerable: true, configurable: true });
      }
    } catch (eQ) {}
  } catch (e) {}
})();
"""

# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install.py).
# Fixes fn.length mismatches (f1 vs f0 ...), installs missing function
# members, and mirrors throwing accessors - all inputs the d8y2
# collector can hash into the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install.py).
# Fixes fn.length mismatches (f1 vs f0 ...), installs missing function
# members, and mirrors throwing accessors - all inputs the d8y2
# collector can hash into the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): the generated install lives in
# work/_arity_block.js (auto-built by tools/gen_arity_install2.py from
# the host-arity survey).  Loaded from disk at import time so the
# 440KB generated block never has to live inside this source file.
# Host arity/shape parity (G4.222 续五): generated install loaded from
# work/_arity_block.js (tools/gen_arity_install2.py).
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): generated install loaded from
# work/_arity_block.js (tools/gen_arity_install2.py, cumulative mode).
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.

# Enumerability parity (G4.255): AUTO-GENERATED by
# tools/gen_enum_install.py from the page-mode surface ledger.
FACE_ENUM_INSTALL = "(function () {\n  // AUTO-GENERATED by tools/gen_enum_install.py from _surface_ledger6.json:\n  // members the reference wheel exposes as enumerable but lv8 defines\n  // non-enumerable (for-in / Object.keys parity).\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"createEvent\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"createEvent\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"open\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"open\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"close\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"close\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"hasFocus\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"hasFocus\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"execCommand\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"execCommand\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandEnabled\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandEnabled\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandState\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandState\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandSupported\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandSupported\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"createExpression\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"createExpression\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"evaluate\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"evaluate\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"getAnimations\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"getAnimations\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"elementFromPoint\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"elementFromPoint\", nd); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"elementsFromPoint\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"elementsFromPoint\", nd); }\n    } catch (e4) {}\n  } } catch (e9) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"createEvent\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"createEvent\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"open\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"open\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"close\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"close\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"hasFocus\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"hasFocus\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"execCommand\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"execCommand\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandEnabled\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandEnabled\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandState\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandState\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"queryCommandSupported\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"queryCommandSupported\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"createExpression\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"createExpression\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"evaluate\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"evaluate\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"getAnimations\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"getAnimations\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"elementFromPoint\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"elementFromPoint\", nd); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"elementsFromPoint\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"elementsFromPoint\", nd); }\n    } catch (e6) {}\n  } } catch (e9) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"timeOrigin\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"timeOrigin\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"onresourcetimingbufferfull\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"onresourcetimingbufferfull\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"clearMarks\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"clearMarks\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"clearMeasures\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"clearMeasures\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"clearResourceTimings\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"clearResourceTimings\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"getEntries\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"getEntries\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"getEntriesByName\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"getEntriesByName\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"getEntriesByType\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"getEntriesByType\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"mark\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"mark\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"measure\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"measure\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"now\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"now\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"setResourceTimingBufferSize\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"setResourceTimingBufferSize\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"toJSON\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"toJSON\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"timing\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"timing\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"navigation\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"navigation\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"memory\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"memory\", nd); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"eventCounts\");\n      if (d && !d.enumerable) {\n        var nd = d.get || d.set\n          ? { get: d.get, set: d.set, enumerable: true, configurable: true }\n          : { value: d.value, writable: d.writable !== false, enumerable: true, configurable: true };\n        Object.defineProperty(O, \"eventCounts\", nd); }\n    } catch (e1) {}\n  } } catch (e9) {}\n})();"

# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Read-only shape parity (G4.256): AUTO-GENERATED by
# tools/gen_shape_install.py from the page-mode surface ledger.
# Read-only descriptor parity (G4.256): AUTO-GENERATED by
# tools/gen_shape_install.py from the page-mode surface ledger.
# G4.264: the reference keeps Element.prototype.style read-only (rw only on
# HTMLElement); earlier passes re-add the setter, so strip it at the end of
# the load.
ELEMENT_STYLE_RO_JS = ("""
(function () {
  try {
    var ep = (typeof Element === 'function') ? Element.prototype : null;
    var hp = (typeof HTMLElement === 'function') ? HTMLElement.prototype : null;
    if (ep && hp && ep !== hp) {
      var d = Object.getOwnPropertyDescriptor(ep, 'style');
      if (d && d.set) {
        // accessor->accessor redefinition is ignored on this object, so
        // delete first and then re-install the getter-only descriptor
        try { delete ep.style; } catch (e0) {}
        Object.defineProperty(ep, 'style', { get: d.get,
          enumerable: d.enumerable === true, configurable: true });
      }
    }
    // G4.264b: the reference exposes HTMLAnchorElement.target as read/write;
    // the table descriptor has no setter, so install one (write-through).
    var ap = (typeof HTMLAnchorElement === 'function')
        ? HTMLAnchorElement.prototype : null;
    if (ap) {
      var td = Object.getOwnPropertyDescriptor(ap, 'target');
      if (td && td.get && !td.set) {
        var targetSet = function (v) {
          try {
            this.setAttribute('target',
              v === null || v === undefined ? '' : String(v));
          } catch (e1) {}
        };
        try { Object.defineProperty(targetSet, 'name',
          {value: 'set target', configurable: true}); } catch (e2) {}
        try { delete ap.target; } catch (e3) {}
        Object.defineProperty(ap, 'target', { get: td.get, set: targetSet,
          enumerable: td.enumerable === true, configurable: true });
      }
    }
  } catch (e) {}
})();
""")

# Value/shape parity (G4.265): AUTO-GENERATED by
# tools/gen_value_shape_install.py from the page-mode surface ledger.
# Value/shape parity (G4.265): AUTO-GENERATED by
# tools/gen_value_shape_install.py from the page-mode surface ledger.
# Value/shape parity (G4.265): AUTO-GENERATED by
# tools/gen_value_shape_install.py from the page-mode surface ledger.
# Value/shape parity (G4.265): AUTO-GENERATED by
# tools/gen_value_shape_install.py from the page-mode surface ledger.
# G4.266: small set-level gaps (members reference owns at a different level or not
# at all): HTMLDivElement.align (write-through), HTMLInputElement.tabIndex
# (belongs to HTMLElement), Screen extras, Performance's EventTarget trio
# (inherited in the reference).
# G4.268: HTMLMediaElement surface (reference-measured 46 members; lv8 had 5).
# Values and shapes come from a fresh <video> probe against the reference.
MEDIA_ELEMENT_FACE_JS = ("""
(function () {
  try {
    var P = (typeof HTMLMediaElement === 'function') ? HTMLMediaElement.prototype : null;
    if (!P) return;
    var store = new WeakMap();
    function slot(o) { var m = store.get(o); if (!m) { m = {}; store.set(o, m); } return m; }
    var VALS = { error: null, src: '', currentSrc: '', crossOrigin: null,
      networkState: 0, preload: 'auto', readyState: 0, seeking: false,
      currentTime: 0, duration: NaN, defaultPlaybackRate: 1, playbackRate: 1,
      ended: false, autoplay: false, loop: false, controls: false, volume: 1,
      muted: false, defaultMuted: false, preservesPitch: true, latencyHint: 0,
      webkitAudioDecodedByteCount: 0, webkitVideoDecodedByteCount: 0 };
    var RW = { src: 1, crossOrigin: 1, preload: 1, currentTime: 1,
      defaultPlaybackRate: 1, playbackRate: 1, autoplay: 1, loop: 1, controls: 1,
      volume: 1, muted: 1, defaultMuted: 1, preservesPitch: 1, latencyHint: 1 };
    Object.keys(VALS).forEach(function (k) {
      if (Object.prototype.hasOwnProperty.call(P, k)) return;
      var d = { get: function () { var s2 = slot(this); return (k in s2) ? s2[k] : VALS[k]; },
        enumerable: true, configurable: true };
      if (RW[k]) {
        d.set = function (v) {
          slot(this)[k] = v;
          if (k === 'src') {
            try { this.setAttribute('src', v === null || v === undefined ? '' : String(v)); } catch (e1) {}
          }
        };
      }
      try { Object.defineProperty(P, k, d); } catch (e2) {}
    });
    ['buffered', 'played', 'seekable', 'controlsList'].forEach(function (k) {
      if (Object.prototype.hasOwnProperty.call(P, k)) return;
      var what = (k === 'controlsList') ? 'DOMTokenList' : 'TimeRanges';
      try {
        Object.defineProperty(P, k, { get: function () {
          throw new Error('HTMLMediaElement.' + k + ' threw C++ exception: ' +
            'ScriptWrappable::GetOrCreateWrapper - ' + what +
            " didn't implement CreateWrapperInternal");
        }, enumerable: true, configurable: true });
      } catch (e3) {}
    });
    ['audioTracks', 'videoTracks', 'textTracks'].forEach(function (k) {
      if (Object.prototype.hasOwnProperty.call(P, k)) return;
      var proto = {};
      try { Object.defineProperty(proto, Symbol.toStringTag,
        { value: 'EventTarget', configurable: true }); } catch (e4) {}
      proto.addEventListener = function () {};
      proto.removeEventListener = function () {};
      proto.dispatchEvent = function () { return true; };
      var lists = new WeakMap();
      try {
        Object.defineProperty(P, k, { get: function () {
          var m = lists.get(this);
          if (!m) { m = Object.create(proto); lists.set(this, m); }
          return m;
        }, enumerable: true, configurable: true });
      } catch (e5) {}
    });
    var members = { load: [0, function () {}],
      addTextTrack: [1, function () { return null; }] };
    Object.keys(members).forEach(function (k) {
      if (Object.prototype.hasOwnProperty.call(P, k)) return;
      var f = members[k][1];
      try { Object.defineProperty(f, 'name', { value: k, configurable: true }); } catch (e6) {}
      try { Object.defineProperty(f, 'length', { value: members[k][0], configurable: true }); } catch (e7) {}
      try { Object.defineProperty(P, k, { value: f, writable: true,
        enumerable: true, configurable: true }); } catch (e8) {}
    });
    // G4.268b: the reference chain is
    //   video/audio -> HTMLVideoElement.prototype -> HTMLMediaElement.prototype
    //                -> HTMLElement.prototype -> ...
    // lv8 linked the concrete prototypes straight to HTMLElement, so the
    // HTMLMediaElement level (and everything on it) was unreachable.
    ['HTMLVideoElement', 'HTMLAudioElement'].forEach(function (n) {
      var c = (typeof window[n] === 'function') ? window[n] : null;
      if (c && c.prototype && Object.getPrototypeOf(c.prototype) !== P) {
        try { Object.setPrototypeOf(c.prototype, P); } catch (e10) {}
      }
    });
    var consts = { NETWORK_EMPTY: 0, NETWORK_IDLE: 1, NETWORK_LOADING: 2,
      NETWORK_NO_SOURCE: 3, HAVE_NOTHING: 0, HAVE_METADATA: 1,
      HAVE_CURRENT_DATA: 2, HAVE_FUTURE_DATA: 3, HAVE_ENOUGH_DATA: 4 };
    Object.keys(consts).forEach(function (k) {
      if (Object.prototype.hasOwnProperty.call(P, k)) return;
      try { Object.defineProperty(P, k, { value: consts[k], writable: false,
        enumerable: true, configurable: true }); } catch (e9) {}
    });
  } catch (e) {}
})();
""")

SMALL_SET_GAPS_JS = ("""
(function () {
  try {
    var dp = (typeof HTMLDivElement === 'function') ? HTMLDivElement.prototype : null;
    if (dp && !Object.prototype.hasOwnProperty.call(dp, 'align')) {
      Object.defineProperty(dp, 'align', {
        get: function () { try { return this.getAttribute('align') || ''; } catch (e) { return ''; } },
        set: function (v) { try { this.setAttribute('align', v === null || v === undefined ? '' : String(v)); } catch (e) {} },
        enumerable: true, configurable: true });
    }
    var ip = (typeof HTMLInputElement === 'function') ? HTMLInputElement.prototype : null;
    if (ip && Object.prototype.hasOwnProperty.call(ip, 'tabIndex')) { try { delete ip.tabIndex; } catch (e) {} }
    // Screen.prototype keeps isExtended/onchange: the page-mode reference
    // carries both (surface-audit P1 check), so deleting them regresses.
    // The arity prelude inserts an intermediate prototype above
    // Performance.prototype; walk the whole chain and drop any own copies of
    // the EventTarget trio so resolution falls through to EventTarget (the
    // reference inherits them, and its dispatch on a non-node target works).
    var evtTrio = ['addEventListener', 'removeEventListener', 'dispatchEvent'];
    try {
      // Performance.prototype itself is not in the live chain (the arity
      // prelude re-parented the instance to an intermediate level), but the
      // reference prototype has 18 own members - strip the trio there too.
      var ppOwn = (typeof Performance === 'function') ? Performance.prototype : null;
      if (ppOwn) {
        evtTrio.forEach(function (k) {
          if (Object.prototype.hasOwnProperty.call(ppOwn, k)) { try { delete ppOwn[k]; } catch (e) {} }
        });
      }
      var chain = [], q = window.performance;
      while (q && q !== Object.prototype) { chain.push(q); q = Object.getPrototypeOf(q); }
      chain.forEach(function (o) {
        if (o === EventTarget.prototype) return;
        evtTrio.forEach(function (k) {
          if (Object.prototype.hasOwnProperty.call(o, k)) { try { delete o[k]; } catch (e) {} }
        });
      });
    } catch (e) {}
  } catch (e) {}
})();
""")

FACE_VALUE_SHAPE_JS = "(function () {\n  // AUTO-GENERATED by tools/gen_value_shape_install.py from _surface_ledger24.json\n  function ro(O, k) {\n    var d = Object.getOwnPropertyDescriptor(O, k); if (!d) return;\n    var get = d.get;\n    if (!get) { var v = d.value; get = function () { return v; }; }\n    try { delete O[k]; } catch (e0) {}\n    try { Object.defineProperty(O, k, { get: get,\n      enumerable: d.enumerable === true, configurable: true }); }\n    catch (e1) {}\n  }\n  function rw(O, k) {\n    var d = Object.getOwnPropertyDescriptor(O, k); if (!d) return;\n    var get = d.get, store = d.get ? d.set : null, val = d.value;\n    if (!get) get = function () { return val; };\n    var set = function (v) {\n      if (store) { try { store.call(this, v); } catch (e2) {} }\n      else { val = v; }\n    };\n    try { delete O[k]; } catch (e3) {}\n    try { Object.defineProperty(O, k, { get: get, set: set,\n      enumerable: d.enumerable === true, configurable: true }); }\n    catch (e4) {}\n  }\n  function fname(O, k, want, arity) {\n    var d = Object.getOwnPropertyDescriptor(O, k); if (!d || !d.value) return;\n    if (want) { try { Object.defineProperty(d.value, 'name',\n      { value: want, configurable: true }); } catch (e5) {} }\n    if (arity >= 0) { try { Object.defineProperty(d.value, 'length',\n      { value: arity, configurable: true }); } catch (e6) {} }\n  }\n  try { var O = (window[\"CSSStyleDeclaration\"] ? CSSStyleDeclaration.prototype : undefined); if (O) fname(O, \"getPropertyPriority\", \"getPropertyPriority\", 1); } catch (e9) {}\n  try { var O = (window[\"CSSStyleDeclaration\"] ? CSSStyleDeclaration.prototype : undefined); if (O) fname(O, \"getPropertyValue\", \"getPropertyValue\", 1); } catch (e9) {}\n  try { var O = (window[\"CSSStyleDeclaration\"] ? CSSStyleDeclaration.prototype : undefined); if (O) fname(O, \"item\", \"item\", 1); } catch (e9) {}\n  try { var O = (window[\"CSSStyleDeclaration\"] ? CSSStyleDeclaration.prototype : undefined); if (O) fname(O, \"removeProperty\", \"removeProperty\", 1); } catch (e9) {}\n  try { var O = (window[\"CSSStyleDeclaration\"] ? CSSStyleDeclaration.prototype : undefined); if (O) fname(O, \"setProperty\", \"setProperty\", 2); } catch (e9) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) rw(O, \"adoptedStyleSheets\"); } catch (e7) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) rw(O, \"adoptedStyleSheets\"); } catch (e7) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"applets\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"applets\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) rw(O, \"body\"); } catch (e7) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) rw(O, \"body\"); } catch (e7) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"compatMode\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"compatMode\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"contentType\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"contentType\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) rw(O, \"designMode\"); } catch (e7) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) rw(O, \"designMode\"); } catch (e7) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) rw(O, \"dir\"); } catch (e7) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) rw(O, \"dir\"); } catch (e7) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) rw(O, \"domain\"); } catch (e7) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) rw(O, \"domain\"); } catch (e7) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"embeds\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"embeds\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"fonts\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"fonts\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"fragmentDirective\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"fragmentDirective\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"fullscreen\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"fullscreen\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"fullscreenElement\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"fullscreenElement\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"pictureInPictureEnabled\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"pictureInPictureEnabled\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"plugins\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"plugins\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"rootElement\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"rootElement\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"styleSheets\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"styleSheets\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitCurrentFullScreenElement\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitCurrentFullScreenElement\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitFullscreenElement\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitFullscreenElement\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitFullscreenEnabled\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitFullscreenEnabled\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitHidden\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitHidden\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitIsFullScreen\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitIsFullScreen\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"webkitVisibilityState\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"webkitVisibilityState\"); } catch (e8) {}\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) ro(O, \"xmlEncoding\"); } catch (e8) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) ro(O, \"xmlEncoding\"); } catch (e8) {}\n  try { var O = (window[\"HTMLFormElement\"] ? HTMLFormElement.prototype : undefined); if (O) fname(O, \"checkValidity\", \"checkValidity\", 0); } catch (e9) {}\n  try { var O = (window[\"HTMLFormElement\"] ? HTMLFormElement.prototype : undefined); if (O) fname(O, \"reportValidity\", \"reportValidity\", 0); } catch (e9) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) ro(O, \"eventCounts\"); } catch (e8) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) ro(O, \"memory\"); } catch (e8) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) ro(O, \"navigation\"); } catch (e8) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) rw(O, \"onresourcetimingbufferfull\"); } catch (e7) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) ro(O, \"timeOrigin\"); } catch (e8) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) ro(O, \"timing\"); } catch (e8) {}\n})();"

FACE_SHAPE_INSTALL = "(function () {\n  // AUTO-GENERATED by tools/gen_shape_install.py from _surface_ledger11.json:\n  // members the reference wheel exposes as getter-only but lv8 defines\n  // read/write - re-define with the getter only.\n  try { var O = (document && Object.getPrototypeOf(document)); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"compatMode\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"compatMode\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"contentType\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"contentType\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e5) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"xmlEncoding\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"xmlEncoding\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"embeds\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"embeds\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"plugins\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"plugins\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"applets\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"applets\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fragmentDirective\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fragmentDirective\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitVisibilityState\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitVisibilityState\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitHidden\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitHidden\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e5) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fullscreen\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fullscreen\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitIsFullScreen\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitIsFullScreen\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitCurrentFullScreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitCurrentFullScreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitFullscreenEnabled\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitFullscreenEnabled\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitFullscreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitFullscreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"rootElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"rootElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"pictureInPictureEnabled\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"pictureInPictureEnabled\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e5) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"styleSheets\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"styleSheets\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fullscreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fullscreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fonts\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fonts\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n  } } catch (e9) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"compatMode\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"compatMode\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"contentType\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"contentType\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"xmlEncoding\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"xmlEncoding\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"embeds\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"embeds\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"plugins\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"plugins\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"applets\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"applets\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fragmentDirective\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fragmentDirective\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e5) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitVisibilityState\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitVisibilityState\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitHidden\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitHidden\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fullscreen\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fullscreen\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitIsFullScreen\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitIsFullScreen\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitCurrentFullScreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitCurrentFullScreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e4) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitFullscreenEnabled\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitFullscreenEnabled\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e1) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"webkitFullscreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"webkitFullscreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e5) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"rootElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"rootElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e2) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"pictureInPictureEnabled\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"pictureInPictureEnabled\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"styleSheets\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"styleSheets\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fullscreenElement\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fullscreenElement\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"fonts\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"fonts\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e4) {}\n  } } catch (e9) {}\n  try { var O = (window[\"Element\"] ? Element.prototype : undefined); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"classList\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"classList\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e3) {}\n    try { var d = Object.getOwnPropertyDescriptor(O, \"attributes\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"attributes\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e0) {}\n  } } catch (e9) {}\n  try { var O = (window[\"HTMLElement\"] ? HTMLElement.prototype : undefined); if (O) {\n    try { var d = Object.getOwnPropertyDescriptor(O, \"dataset\");\n      if (d && d.get && d.set) {\n        Object.defineProperty(O, \"dataset\", { get: d.get, enumerable: d.enumerable === true, configurable: true }); }\n    } catch (e6) {}\n  } } catch (e9) {}\n})();"

# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
# Host arity/shape parity (G4.222 续五): AUTO-GENERATED from the
# host-arity survey against the reference (tools/gen_arity_install2.py).
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.
FACE_ARITY_INSTALL = "(function () {\n  // AUTO-GENERATED by tools/gen_arity_install2.py from\n  // work/reference-traces/host-arity-survey.json (G4.222 \u7eed\u4e94):\n  // (object, member, shape/arity) parity with the reference face.\n  function mkStub(name, length) {\n    var f = function () { return undefined; };\n    try { Object.defineProperty(f, 'name', { value: name }); } catch (e0) {}\n    try { Object.defineProperty(f, 'length', { value: length }); } catch (e1) {}\n    return f;\n  }\n  var II_MSG = 'Illegal invocation';\n  try { Object.getOwnPropertyDescriptor(Node.prototype, 'textContent').get.call(undefined); }\n  catch (e6) { II_MSG = String(e6.message || e6); }\n  var II_MSG = 'Illegal invocation';\n  try { Object.getOwnPropertyDescriptor(Node.prototype, 'textContent').get.call(undefined); }\n  catch (e6) { II_MSG = String(e6.message || e6); }\n  try { var O = (window[\"CanvasRenderingContext2D\"] ? CanvasRenderingContext2D.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"arc\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"arcTo\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bezierCurveTo\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"clearRect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createConicGradient\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createImageData\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createLinearGradient\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createPattern\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createRadialGradient\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"drawImage\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"ellipse\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 7)\n        Object.defineProperty(f.value, 'length', { value: 7, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"fillRect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"fillText\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getImageData\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isPointInPath\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isPointInStroke\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"lineTo\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"measureText\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"moveTo\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"putImageData\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"quadraticCurveTo\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"rect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"rotate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"roundRect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"scale\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setLineDash\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setTransform\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"strokeRect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"strokeText\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"transform\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"translate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window.Crypto ? Crypto.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getRandomValues\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"CustomEvent\"] ? CustomEvent.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"constructor\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"initCustomEvent\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"DOMTokenList\"] ? DOMTokenList.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"contains\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"item\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"replace\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"supports\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"toggle\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"forEach\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Document\"] ? Document.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"adoptNode\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"caretPositionFromPoint\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createAttributeNS\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createCDATASection\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createComment\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createElement\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createNSResolver\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createNodeIterator\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createProcessingInstruction\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createTextNode\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createTreeWalker\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"evaluate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"execCommand\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementById\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByClassName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByTagName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByTagNameNS\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"importNode\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"moveBefore\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"queryCommandEnabled\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"queryCommandIndeterm\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"queryCommandState\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"queryCommandSupported\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"queryCommandValue\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"querySelector\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"querySelectorAll\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Element\"] ? Element.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"ariaNotify\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"attachShadow\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"closest\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getAttributeNode\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByClassName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getElementsByTagName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"hasAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"hasPointerCapture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"insertAdjacentHTML\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"querySelector\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"querySelectorAll\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"releasePointerCapture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"removeAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setAttributeNode\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setPointerCapture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"toggleAttribute\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"EventTarget\"] ? EventTarget.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"addEventListener\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"dispatchEvent\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"removeEventListener\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"FormData\"] ? FormData.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"append\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"delete\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"forEach\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"get\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getAll\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"has\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"set\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"HTMLCanvasElement\"] ? HTMLCanvasElement.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"toBlob\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"HTMLCollection\"] ? HTMLCollection.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"item\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"namedItem\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"HTMLInputElement\"] ? HTMLInputElement.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setCustomValidity\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setRangeText\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setSelectionRange\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"HTMLMediaElement\"] ? HTMLMediaElement.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"canPlayType\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Headers\"] ? Headers.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"append\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"delete\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"forEach\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"get\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"has\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"set\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window.History ? History.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"pushState\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"replaceState\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"IDBDatabase\"] ? IDBDatabase.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createObjectStore\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteObjectStore\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"transaction\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"IDBObjectStore\"] ? IDBObjectStore.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"add\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createIndex\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"delete\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteIndex\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"get\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getKey\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"index\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"put\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"IDBTransaction\"] ? IDBTransaction.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"objectStore\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"MutationObserver\"] ? MutationObserver.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"constructor\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"observe\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Navigator\"] ? Navigator.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"sendBeacon\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vibrate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Node\"] ? Node.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"appendChild\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"compareDocumentPosition\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"contains\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"insertBefore\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isDefaultNamespace\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"lookupPrefix\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"removeChild\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"replaceChild\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"NodeList\"] ? NodeList.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"item\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"forEach\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Performance\"] ? Performance.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getEntriesByName\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getEntriesByType\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"mark\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"measure\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setResourceTimingBufferSize\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = Response; if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"json\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"redirect\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"Text\"] ? Text.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"splitText\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"WebGLRenderingContext\"] ? WebGLRenderingContext.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"activeTexture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"attachShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bindAttribLocation\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bindBuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bindFramebuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bindRenderbuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bindTexture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"blendColor\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"blendEquation\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"blendEquationSeparate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"blendFunc\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"blendFuncSeparate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bufferData\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"bufferSubData\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"checkFramebufferStatus\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"clear\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"clearColor\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"clearDepth\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"clearStencil\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"colorMask\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"compileShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"compressedTexImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 7)\n        Object.defineProperty(f.value, 'length', { value: 7, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"compressedTexSubImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 8)\n        Object.defineProperty(f.value, 'length', { value: 8, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"copyTexImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 8)\n        Object.defineProperty(f.value, 'length', { value: 8, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"copyTexSubImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 8)\n        Object.defineProperty(f.value, 'length', { value: 8, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"createShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"cullFace\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteBuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteFramebuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteProgram\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteRenderbuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"deleteTexture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"depthFunc\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"depthMask\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"depthRange\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"detachShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"disable\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"disableVertexAttribArray\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"drawArrays\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"drawElements\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"enable\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"enableVertexAttribArray\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"framebufferRenderbuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"framebufferTexture2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"frontFace\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"generateMipmap\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getActiveAttrib\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getActiveUniform\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getAttachedShaders\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getAttribLocation\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getBufferParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getExtension\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getFramebufferAttachmentParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getProgramInfoLog\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getProgramParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getRenderbufferParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getShaderInfoLog\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getShaderParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getShaderPrecisionFormat\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getShaderSource\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getTexParameter\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getUniform\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getUniformLocation\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getVertexAttrib\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getVertexAttribOffset\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"hint\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isBuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isEnabled\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isFramebuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isProgram\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isRenderbuffer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isShader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"isTexture\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"lineWidth\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"linkProgram\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"pixelStorei\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"polygonOffset\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"readPixels\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 7)\n        Object.defineProperty(f.value, 'length', { value: 7, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"renderbufferStorage\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"sampleCoverage\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"scissor\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"shaderSource\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilFunc\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilFuncSeparate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilMask\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilMaskSeparate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilOp\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"stencilOpSeparate\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"texImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"texParameterf\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"texParameteri\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"texSubImage2D\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 7)\n        Object.defineProperty(f.value, 'length', { value: 7, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform1f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform1fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform1i\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform1iv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform2f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform2fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform2i\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform2iv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform3f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform3fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform3i\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform3iv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform4f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform4fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform4i\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniform4iv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniformMatrix2fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniformMatrix3fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"uniformMatrix4fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"useProgram\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"validateProgram\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib1f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib1fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib2f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 3)\n        Object.defineProperty(f.value, 'length', { value: 3, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib2fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib3f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib3fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib4f\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 5)\n        Object.defineProperty(f.value, 'length', { value: 5, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttrib4fv\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"vertexAttribPointer\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 6)\n        Object.defineProperty(f.value, 'length', { value: 6, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"viewport\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 4)\n        Object.defineProperty(f.value, 'length', { value: 4, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = (window[\"XMLHttpRequest\"] ? XMLHttpRequest.prototype : undefined); if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"getResponseHeader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"open\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"overrideMimeType\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    try { var f = Object.getOwnPropertyDescriptor(O, \"setRequestHeader\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 2)\n        Object.defineProperty(f.value, 'length', { value: 2, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  try { var O = console; if (O) {\n    try { var f = Object.getOwnPropertyDescriptor(O, \"context\");\n      if (f && f.value && typeof f.value === 'function' &&\n          f.value.length !== 1)\n        Object.defineProperty(f.value, 'length', { value: 1, configurable: true });\n    } catch (e2) {}\n    } } catch (e9) {}\n  // Screen: reference chains Screen.prototype into EventTarget; an own\n  // trio would diverge on gOPN - remove and rewire instead.\n  try {\n    if (window.Screen && Screen.prototype &&\n        typeof EventTarget === 'function') {\n      ['addEventListener', 'removeEventListener', 'dispatchEvent']\n        .forEach(function (k) {\n          if (Object.prototype.hasOwnProperty.call(Screen.prototype, k)) {\n            try { delete Screen.prototype[k]; } catch (eH) {} }\n        });\n      if (Object.getPrototypeOf(Screen.prototype) !== EventTarget.prototype) {\n        try { Object.setPrototypeOf(Screen.prototype,\n          EventTarget.prototype); } catch (eI) {}\n      }\n      if (Object.prototype.hasOwnProperty.call(screen, 'isExtended')) {\n        try { delete screen.isExtended; } catch (eJ) {}\n      }\n    }\n  } catch (eK) {}\n  // performance: the reference's instance gOPN is empty and the 20\n  // enumerable members live one level down - insert an intermediate\n  // prototype level.\n  try {\n    var perf = window.performance;\n    var PP = window.Performance && Performance.prototype;\n    if (perf && PP) {\n      var names = ['timeOrigin', 'onresourcetimingbufferfull',\n        'clearMarks', 'clearMeasures', 'clearResourceTimings',\n        'getEntries', 'getEntriesByName', 'getEntriesByType', 'mark',\n        'measure', 'now', 'setResourceTimingBufferSize', 'toJSON',\n        'timing', 'navigation', 'memory', 'eventCounts',\n        'addEventListener', 'removeEventListener', 'dispatchEvent'];\n      var IP = Object.create((typeof EventTarget === 'function' && EventTarget.prototype) ? EventTarget.prototype : Object.getPrototypeOf(PP));\n      names.forEach(function (k) {\n        var d = Object.getOwnPropertyDescriptor(perf, k) ||\n                Object.getOwnPropertyDescriptor(PP, k);\n        if (!d) {\n          try { Object.defineProperty(IP, k, { value: mkStub(k, 0),\n            writable: true, enumerable: true, configurable: true });\n          } catch (eQ) {}\n          return;\n        }\n        try {\n          if (d.get || d.set) {\n            Object.defineProperty(IP, k, { get: d.get, set: d.set,\n              enumerable: true, configurable: true });\n          } else {\n            Object.defineProperty(IP, k, { value: d.value,\n              writable: true, enumerable: true, configurable: true });\n          }\n        } catch (eL) {}\n      });\n      names.forEach(function (k) {\n        if (Object.prototype.hasOwnProperty.call(perf, k)) {\n          try { delete perf[k]; } catch (eM) {} }\n      });\n      try { Object.setPrototypeOf(perf, IP); } catch (eN) {}\n    }\n  } catch (eP) {}\n})();"
# Fixes fn.length mismatches, installs missing members/constants,
# wraps prototype-readable accessors with brand checks (the reference
# throws Illegal invocation when an accessor's this is the prototype),
# deletes lv8-only members, and adjusts the screen/performance
# prototype structure - all inputs the d8y2 collector can hash into
# the packed state bytes.

FUNCTION_NAME_SWEEP_INSTALL = """
(function () {
  try {
    function rename(f, nm) {
      try {
        if (typeof f === 'function' && !f.name)
          Object.defineProperty(f, 'name',
            {value: nm, configurable: true});
      } catch (e) {}
    }
    function sweep(proto) {
      Object.getOwnPropertyNames(proto).forEach(function (k) {
        var d;
        try { d = Object.getOwnPropertyDescriptor(proto, k); } catch (e) { return; }
        if (!d) return;
        if (typeof d.value === 'function') rename(d.value, k);
        if (d.get) rename(d.get, 'get ' + k);
        if (d.set) rename(d.set, 'set ' + k);
      });
    }
    Object.getOwnPropertyNames(window).forEach(function (k) {
      try {
        var v = window[k];
        if (typeof v === 'function' && v.prototype &&
            Object.prototype.toString.call(v.prototype) === '[object Object]')
          sweep(v.prototype);
      } catch (e) {}
    });
  } catch (e) {}
})();
"""

FORM_IDL_FACE_INSTALL = """
(function () {
  try {
    if (typeof HTMLFormElement === 'undefined' || !HTMLFormElement.prototype)
      return;
    var hfp = HTMLFormElement.prototype;
    function reflected(attr, dflt) {
      return function () {
        var v = this.getAttribute(attr);
        return v === null || v === undefined ? dflt : v;
      };
    }
    function setReflected(attr) {
      return function (v) {
        try { this.setAttribute(attr, String(v)); } catch (_) {}
      };
    }
    var defs = {
      method: { get: reflected('method', 'get'),
                set: setReflected('method') },
      enctype: { get: reflected('enctype', 'application/x-www-form-urlencoded'),
                 set: setReflected('enctype') },
      encoding: { get: reflected('enctype', 'application/x-www-form-urlencoded'),
                  set: setReflected('enctype') },
      acceptCharset: { get: reflected('accept-charset', 'UTF-8'),
                       set: setReflected('accept-charset') },
      autocomplete: { get: reflected('autocomplete', 'on'),
                      set: setReflected('autocomplete') }
    };
    Object.keys(defs).forEach(function (k) {
      try {
        Object.defineProperty(hfp, k, {
          get: defs[k].get, set: defs[k].set,
          enumerable: true, configurable: true });
      } catch (e) {}
    });
    try {
      Object.defineProperty(hfp, 'target', {
        get: function () {
          var v = this.getAttribute('target');
          return v === null || v === undefined ? '_self' : v;
        },
        set: function (v) {
          try { this.setAttribute('target', String(v)); } catch (_) {}
        },
        enumerable: true, configurable: true });
    } catch (e) {}
    try {
      Object.defineProperty(hfp, 'noValidate', {
        get: function () { return this.hasAttribute('novalidate'); },
        set: function (v) {
          try {
            if (v) this.setAttribute('novalidate', '');
            else this.removeAttribute('novalidate');
          } catch (_) {}
        },
        enumerable: true, configurable: true });
    } catch (e) {}
    // action: reflected URL - an absent attribute reads '' (NOT the
    // document URL); a present one reads resolved against the base
    try {
      Object.defineProperty(hfp, 'action', {
        get: function () {
          var v = this.getAttribute('action');
          if (v === null || v === undefined || v === '') return '';
          try {
            return new URL(v, this.ownerDocument && this.ownerDocument.location
              && this.ownerDocument.location.href || undefined).href;
          } catch (e2) { return v; }
        },
        set: function (v) {
          try { this.setAttribute('action', String(v)); } catch (_) {}
        },
        enumerable: true, configurable: true });
    } catch (e) {}
    try {
      Object.defineProperty(hfp, 'checkValidity', {
        value: function () { return true; },
        writable: true, enumerable: true, configurable: true });
      Object.defineProperty(hfp, 'reportValidity', {
        value: function () { return true; },
        writable: true, enumerable: true, configurable: true });
    } catch (e) {}
  } catch (e) {}
})();
"""

INPUT_IDL_DEFAULTS_INSTALL = """
(function () {
  try {
    if (typeof HTMLInputElement === 'undefined' || !HTMLInputElement.prototype)
      return;
    var hip = HTMLInputElement.prototype;
    function reflected(attr, dflt, numeric) {
      return function () {
        var v = this.getAttribute(attr);
        if (v === null || v === undefined) return numeric ? dflt : String(dflt);
        return numeric ? (parseInt(v, 10) || 0) : v;
      };
    }
    function setReflected(attr) {
      return function (v) {
        try { this.setAttribute(attr, String(v)); } catch (_) {}
      };
    }
    var defs = {
      type: { get: reflected('type', 'text', false),
              set: setReflected('type') },
      size: { get: reflected('size', 20, true),
              set: setReflected('size') },
      maxLength: { get: reflected('maxlength', -1, true),
                   set: setReflected('maxlength') },
      minLength: { get: reflected('minlength', -1, true),
                   set: setReflected('minlength') },
      tabIndex: { get: reflected('tabindex', -1, true),
                  set: setReflected('tabindex') }
    };
    Object.keys(defs).forEach(function (k) {
      try {
        Object.defineProperty(hip, k, {
          get: defs[k].get, set: defs[k].set,
          enumerable: true, configurable: true });
      } catch (e) {}
    });
    try {
      Object.defineProperty(hip, 'labels', {
        get: function () { return null; },
        enumerable: true, configurable: true });
    } catch (e) {}
  } catch (e) {}
})();
"""

# G4.388: per-load document-chain wire.  The native host lays a merged
# document level; after all page-load face installers have finished, split
# it into the reference's HTMLDocument (constructor only) and Document
# levels without relocating the native accessors themselves.
DOCUMENT_CHAIN_TWO_LEVEL_WIRE_JS = r"""(function(){
  try {
    if (typeof HTMLDocument !== 'function' || typeof Document !== 'function' ||
        typeof Node !== 'function') return;
    var oldHP = HTMLDocument.prototype, oldDP = Document.prototype;
    if (!oldHP || !oldDP) return;
    // The generated face made several oldDP descriptors non-configurable.
    // Rebuild an equivalent Document level so the live native descriptors
    // can replace their inert placeholders without changing its visible key
    // set or the reference chain shape.
    var DP = Object.create(Node.prototype);
    var skipNative = {childNodes:1, firstChild:1, baseURI:1, contains:1,
      implementation:1, documentElement:1, head:1, body:1};
    Reflect.ownKeys(oldDP).forEach(function (key) {
      try {
        if (typeof key === 'string' &&
            (skipNative[key] ||
             (Object.prototype.hasOwnProperty.call(oldHP, key) && key !== 'constructor')))
          return;
        Object.defineProperty(DP, key, Object.getOwnPropertyDescriptor(oldDP, key));
      } catch (e000) {}
    });
    try { Document.prototype = DP; } catch (e000a) { return; }
    // Keep each native DOMImplementation in an invisible closure slot.  A
    // page reload creates a fresh document after the boot document installed
    // a getter-only prototype member, so a normal assignment cannot retain
    // the new instance. Re-home it before making the public face
    // Document.prototype-only; derived documents enroll through the symbol
    // hook below.
    var implementationSlots = new WeakMap();
    function makeImplementation() {
      var impl = {};
      try { if (typeof DOMImplementation === 'function' && DOMImplementation.prototype)
        Object.setPrototypeOf(impl, DOMImplementation.prototype); } catch (e00a) {}
      function named(name, length, fn) {
        try { Object.defineProperty(fn, 'name', {value:name, configurable:true});
          Object.defineProperty(fn, 'length', {value:length, configurable:true}); } catch (e00b) {}
        try { Object.defineProperty(impl, name,
          {value:fn, writable:true, enumerable:true, configurable:true}); } catch (e00c) {}
      }
      named('createHTMLDocument', 1, function (title) {
        var text = '<!DOCTYPE html><html><head><title>' + String(title == null ? '' : title)
          .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
          + '</title></head><body></body></html>';
        return new DOMParser().parseFromString(text, 'text/html');
      });
      named('createDocument', 3, function (namespaceURI, qualifiedName) {
        return new DOMParser().parseFromString('<html></html>', 'text/html');
      });
      named('createDocumentType', 3, function (name, publicId, systemId) {
        return {name:String(name || ''), publicId:String(publicId || ''),
          systemId:String(systemId || '')};
      });
      named('hasFeature', 2, function () { return true; });
      return impl;
    }
    function installImplementation(doc, provided) {
      try {
        if (provided && (typeof provided === 'object' ||
                         typeof provided === 'function'))
          implementationSlots.set(doc, provided);
        var own = Object.getOwnPropertyDescriptor(doc, 'implementation');
        if (own && Object.prototype.hasOwnProperty.call(own, 'value')) {
          if (!implementationSlots.has(doc)) implementationSlots.set(doc, own.value);
          delete doc.implementation;
        }
      } catch (e00) {}
    }
    installImplementation(document);
    try { Object.defineProperty(DP, 'implementation', {
      enumerable: true, configurable: true,
      get: function () {
        var value = implementationSlots.get(this);
        if (!value) { value = makeImplementation(); implementationSlots.set(this, value); }
        return value;
      }
    }); } catch (e01) {}
    // The final face normalizer's documentElement/head/body placeholders are
    // shape-correct but inert. Query through the retained native selector;
    // wrapper identity and the element prototype chain then come from the
    // same host path as reference's live document accessors.  reference's Document
    // prototype body is an accessor/rw (G4.389 ledger value_diff): the
    // setter parks the assignment in a per-document slot the getter honors,
    // while documentElement/head stay getter-only like the reference.
    var bodySlots = new WeakMap();
    [['documentElement', 'html'], ['head', 'head'], ['body', 'body']]
      .forEach(function (pair) {
        try {
          var desc = {enumerable: true, configurable: true,
            get: function () {
              if (pair[0] === 'body') {
                try { if (bodySlots.has(this)) return bodySlots.get(this); }
                catch (e01d) {}
              }
              try { return this.querySelector(pair[1]); } catch (e01a) { return null; }
            }};
          if (pair[0] === 'body')
            desc.set = function (v) {
              try { bodySlots.set(this, v); } catch (e01c) {}
            };
          Object.defineProperty(DP, pair[0], desc);
        } catch (e01b) {}
      });
    // G4.391: park the installer on the internal string-keyed bridge, not
    // on a Symbol.for globalThis key - getOwnPropertySymbols sees the
    // latter (reference's window carries ZERO own symbols; same D2 class as the
    // G4.308 c9/sheetCache move).  Native MakeDocumentObject reads it via
    // InternalBridgeGet.
    try { var __docBridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
      if (__docBridge) __docBridge.lv8DocumentImplementationInstall = installImplementation; }
    catch (e02) {}
    // The page host initially exposes its working members on the merged
    // HTMLDocument level. Keep those exact native descriptors at their final
    // Document level before making HTMLDocument constructor-only; same-name
    // face placeholders are intentionally overwritten, while the few Node
    // members below stay at Node.prototype.
    Object.getOwnPropertyNames(oldHP).forEach(function (key) {
      if (key === 'constructor' || skipNative[key]) return;
      try {
        var descriptor = Object.getOwnPropertyDescriptor(oldHP, key);
        if (descriptor) Object.defineProperty(DP, key, descriptor);
      } catch (e03) {}
    });
    // oldHP has non-configurable face placeholders which cannot be deleted.
    // Replace it with a fresh constructor-only level, retaining the exact
    // two-level document chain without letting those placeholders shadow DP.
    var HP = Object.create(DP);
    try { Object.defineProperty(HP, 'constructor',
      {value: HTMLDocument, writable: true, enumerable: false, configurable: true});
      HTMLDocument.prototype = HP;
    } catch (e03a) { return; }
    Object.setPrototypeOf(DP, Node.prototype);
    Object.setPrototypeOf(document, HP);
    // FACE_NORMALIZE installs a transitional own referrer before the
    // Document level exists.  The reference resolves it from Document as an
    // accessor (G4.389 ledger: lv8 answered a plain string after the
    // placeholder rows went away), so re-home the captured value there.
    var referrerVal = '';
    try { referrerVal = String(document.referrer || ''); } catch (e0c) {}
    try { delete document.referrer; } catch (e0) {}
    try {
      var refGet = function () { return referrerVal; };
      try { Object.defineProperty(refGet, 'name',
        {value: 'get referrer', configurable: true}); } catch (e0d) {}
      try { delete DP.referrer; } catch (e0e) {}
      Object.defineProperty(DP, 'referrer',
        {get: refGet, enumerable: true, configurable: true});
    } catch (e0f) {}
    ['childNodes', 'firstChild', 'baseURI', 'contains'].forEach(function (key) {
      try { delete DP[key]; } catch (e2) {}
    });
    try { Object.defineProperty(DP, 'constructor',
      {value: Document, writable: true, enumerable: false, configurable: true}); }
    catch (e3) {}
    try { Object.defineProperty(DP, Symbol.toStringTag,
      {value: 'Document', writable: false, enumerable: false, configurable: true}); }
    catch (e4) {}
  } catch (e5) {}
})();"""

PAGE_LOAD_PRELUDE = (
    "(function(){"
    "var tracksStub=function(){var o={addEventListener:function(){},"
    "removeEventListener:function(){},dispatchEvent:function(){}};"
    "try{Object.defineProperty(o,Symbol.toStringTag,"
    "{value:'EventTarget',configurable:true});}catch(e){}return o;};"
    # The first DOM query creates native wrappers.  Rebuild their public
    # prototype hierarchy before that happens; otherwise the cached page
    # nodes retain an obsolete bare template while later DOMParser nodes do
    # not.
    "var b=globalThis.__iv8__&&globalThis.__iv8__.__lv8_internal__;"
    "if(b&&typeof b.fixElementHierarchy==='function')"
    "b.fixElementHierarchy();"
    "var node=document.querySelector('[data-lv8-document-location]');"
    "if(node&&node.parentNode)node.parentNode.removeChild(node);"
    "var fnode=document.querySelector('[data-lv8-face-norm]');"
    "if(fnode&&fnode.parentNode)fnode.parentNode.removeChild(fnode);"
    "var xnode=document.querySelector('[data-lv8-xpath-early]');"
    "if(xnode&&xnode.parentNode)xnode.parentNode.removeChild(xnode);"
    "try{window.__g372=[!!document.querySelector('[data-lv8-face-norm]'),!!document.querySelector('[data-lv8-xpath-early]'),document.readyState,!!(document.body||document.documentElement)];}catch(e373){window.__g372=['ERR:'+e373];}"
    # reference exposes these constructors on window; lv8's DOM layer doesn't.
    # They must exist before the CDATASection/HTMLFrameSetElement fixes
    # below reach through their prototypes.  Bound through wrapNative so
    # toString renders the native form the reference shows.
    "try{var lv8Ctor=function(name,len){"
    "var f=function(){return Object.create(f.prototype);};"
    "try{Object.defineProperty(f,'name',{value:name,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(f,'length',{value:len,configurable:true});}"
    "catch(e){}"
    "try{if(globalThis.__lv8__&&globalThis.__lv8__.wrapNative)"
    "globalThis.__lv8__.wrapNative(f,name);}catch(e){}"
    "return f;};"
    "[['Path2D',0],['SourceBuffer',0],['SVGPatternElement',0],"
    "['CDATASection',0],['HTMLFrameSetElement',0],"
    "['OffscreenCanvasRenderingContext2D',0],['TextTrackList',0],"
    "['ScreenOrientation',0],['PerformancePaintTiming',0],"
    "['MediaEncryptedEvent',1],['BeforeInstallPromptEvent',1]]"
    ".forEach(function(e){"
    "if(typeof window[e[0]]==='function')return;"
    "try{Object.defineProperty(window,e[0],{value:lv8Ctor(e[0],e[1]),"
    "writable:true,configurable:true,enumerable:true});}catch(e2){}});"
    # reference's prototype members the bitfield probes observe
    "try{if(window.Path2D&&!Path2D.prototype.addPath){"
    "var ap=function addPath(path){};"
    "try{Object.defineProperty(ap,'length',{value:1,configurable:true});}"
    "catch(e3){}"
    "try{if(globalThis.__lv8__)globalThis.__lv8__.wrapNative(ap,'addPath');}"
    "catch(e3){}"
    "Object.defineProperty(Path2D.prototype,'addPath',"
    "{value:ap,writable:true,configurable:true,enumerable:true});}}"
    "catch(e){}"
    "try{if(window.TextTrackList&&!TextTrackList.prototype.getTrackById){"
    "var gt=function getTrackById(id){return null;};"
    "try{Object.defineProperty(gt,'length',{value:1,configurable:true});}"
    "catch(e3){}"
    "try{if(globalThis.__lv8__)globalThis.__lv8__.wrapNative(gt,'getTrackById');}"
    "catch(e3){}"
    "Object.defineProperty(TextTrackList.prototype,'getTrackById',"
    "{value:gt,writable:true,configurable:true,enumerable:true});}}"
    "catch(e){}}"
    "catch(e){}"
    "try{if(typeof XMLHttpRequest!=='undefined'&&XMLHttpRequest.prototype){"
    "var H=['onabort','onerror','onload','onloadend','onloadstart',"
    "'onprogress','ontimeout'];"
    "var E=Object.create(Object.getPrototypeOf(XMLHttpRequest.prototype));"
    "H.forEach(function(h){try{Object.defineProperty(E,h,"
    "{value:null,writable:true,configurable:true});}catch(e){}});"
    "Object.setPrototypeOf(XMLHttpRequest.prototype,E);}}catch(e){}"
    "var cPF=function canPlayType(t){return 'maybe';};"
    # G4.379: this legacy per-tag media face predates the G4.268 chain fix
    # (video/audio -> HTMLMediaElement.prototype -> HTMLElement).  With the
    # proper interface present it would clone the whole media face onto the
    # CONCRETE tag prototypes (audio 34 own, video 42 own vs the reference's
    # constructor-only / 9-member intermediates), splitting the element
    # for-in key order the challenge SDK's media block samples.  The
    # HTMLMediaElement-level face plus the family tables cover everything,
    # so the legacy path only runs when the interface is missing entirely.
    "try{['audio','video'].forEach(function(t){"
    "var mp=Object.getPrototypeOf(document.createElement(t));"
    "if(!mp||typeof HTMLMediaElement==='function')return;"
    "if(Object.prototype.hasOwnProperty.call(mp,'canPlayType')&&"
    "mp.canPlayType.length===1)return;"
    "try{Object.defineProperty(mp,'canPlayType',{value:cPF,"
    "writable:true,configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'play'))"
    "try{Object.defineProperty(mp,'play',{value:function(){},"
    "writable:true,configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'pause'))"
    "try{Object.defineProperty(mp,'pause',{value:function(){},"
    "writable:true,configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'paused'))"
    "try{Object.defineProperty(mp,'paused',{get:function(){return true;},"
    "configurable:true,enumerable:true});}catch(e){}"
    "var IV={src:'',currentSrc:'',duration:NaN,currentTime:0,ended:false,"
    "error:null,networkState:0,readyState:0,seeking:false,controls:false,"
    "autoplay:false,loop:false,preload:'auto',defaultPlaybackRate:1,"
    "defaultMuted:false,muted:false,volume:1,playbackRate:1,crossOrigin:null,"
    "webkitAudioDecodedByteCount:0,webkitVideoDecodedByteCount:0};"
    "if(t==='video'){IV.playsInline=false;IV.height=0;IV.width=0;"
    "IV.poster='';IV.videoHeight=0;IV.videoWidth=0;}"
    "Object.keys(IV).forEach(function(k){"
    "if(Object.prototype.hasOwnProperty.call(mp,k))return;"
    "var vv=IV[k];"
    "try{Object.defineProperty(mp,k,{get:function(){return vv;},"
    "configurable:true,enumerable:true});}catch(e){}});"
    "['seekable','buffered','controlsList'].forEach(function(k){"
    "if(Object.prototype.hasOwnProperty.call(mp,k))return;"
    "try{Object.defineProperty(mp,k,{get:function(){"
    "var what=(k==='controlsList')?'DOMTokenList':'TimeRanges';"
    "throw new Error('HTMLMediaElement.'+k+' threw C++ exception: '"
    "+ 'ScriptWrappable::GetOrCreateWrapper - '+what+' didn'"
    "+String.fromCharCode(39)+'t implement CreateWrapperInternal');},"
    "configurable:true,enumerable:true});}catch(e){}});"
    "if(!Object.prototype.hasOwnProperty.call(mp,'videoTracks'))"
    "try{var vt={};try{Object.defineProperty(vt,Symbol.toStringTag,{value:'EventTarget',configurable:true});}catch(e){}"
    "vt.addEventListener=function(){};vt.removeEventListener=function(){};"
    "Object.defineProperty(mp,'videoTracks',{get:function(){return vt;},configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'audioTracks'))"
    "try{var at2={};try{Object.defineProperty(at2,Symbol.toStringTag,{value:'EventTarget',configurable:true});}catch(e){}"
    "at2.addEventListener=function(){};at2.removeEventListener=function(){};"
    "Object.defineProperty(mp,'audioTracks',{get:function(){return at2;},configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'textTracks'))"
    "try{var tt={};try{Object.defineProperty(tt,Symbol.toStringTag,"
    "{value:'EventTarget',configurable:true});}catch(e){}"
    "tt.addEventListener=function(){};tt.removeEventListener=function(){};"
    "Object.defineProperty(mp,'textTracks',{get:function(){return tt;},"
    "configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'addTextTrack'))"
    "try{var atf=function(){return null;};"
    "try{Object.defineProperty(atf,'name',{value:'addTextTrack'});}catch(e){}"
    "try{Object.defineProperty(atf,'length',{value:1});}catch(e){}"
    "Object.defineProperty(mp,'addTextTrack',{value:atf,writable:true,"
    "configurable:true,enumerable:true});}catch(e){}"
    "if(!Object.prototype.hasOwnProperty.call(mp,'load'))"
    "try{var lf=function(){};"
    "try{Object.defineProperty(lf,'name',{value:'load'});}catch(e){}"
    "try{Object.defineProperty(lf,'length',{value:0});}catch(e){}"
    "Object.defineProperty(mp,'load',{value:lf,writable:true,"
    "configurable:true,enumerable:true});}catch(e){}"
    "});}catch(e){}"
    "try{if(window.chrome&&window.chrome.app&&"
    "!window.chrome.app.InstallState)"
    "Object.defineProperty(window.chrome.app,'InstallState',"
    "{value:{DISABLED:'disabled',INSTALLED:'installed',"
    "NOT_INSTALLED:'not_installed'},configurable:true,enumerable:true});}"
    "catch(e){}"
    "try{if(typeof HTMLFrameSetElement==='function'&&"
    "!Object.prototype.hasOwnProperty.call(HTMLFrameSetElement.prototype,"
    "'hasPointerCapture'))"
    "Object.defineProperty(HTMLFrameSetElement.prototype,"
    "'hasPointerCapture',{value:function(){return false;},writable:true,"
    "configurable:true,enumerable:true});}catch(e){}"
    "try{if(window.external&&typeof external.IsSearchProviderInstalled==="
    "'undefined')"
    "Object.defineProperty(window.external,'IsSearchProviderInstalled',"
    "{value:function(){return false;},writable:true,configurable:true,"
    "enumerable:true});}catch(e){}"
    "try{if(typeof CDATASection==='function'&&!Object.prototype."
    "hasOwnProperty.call(CDATASection.prototype,'remove'))"
    "Object.defineProperty(CDATASection.prototype,'remove',"
    "{value:function(){},writable:true,configurable:true,enumerable:true});}"
    "catch(e){}"
    "try{if(typeof navigator.mediaCapabilities==='object'&&"
    "navigator.mediaCapabilities&&typeof navigator.mediaCapabilities."
    "decodingInfo!=='function')Object.defineProperty("
    "navigator.mediaCapabilities,'decodingInfo',{value:function(){"
    "return Promise.resolve({powerEfficient:true,smooth:true,"
    "supported:true});},writable:true,configurable:true,enumerable:true});}"
    "catch(e){}"
    # Navigator surface: the reference wheel keeps all members on
    # Navigator.prototype (53 native-shaped get-only accessors + 16 data
    # methods, exact measured order; instance own = [sendBeacon]).  lv8
    # hangs them on the navigator instance, so the SDK's synchronous
    # getOwnPropertyDescriptors(Navigator.prototype) + getter toString
    # check fails and its iframe window-binding probe never runs.  Must
    # run in the prelude: the check happens while page scripts load,
    # before POST_LOAD_PRELUDE gets a chance.
    "(function(){try{var NP=window.Navigator&&Navigator.prototype;if(NP&&NP!==Object.prototype){var CAP={};['javaEnabled','getBattery','constructor'].forEach(function(k){var dsc=Object.getOwnPropertyDescriptor(NP,k);if(dsc)CAP[k]=dsc.value!==undefined?dsc.value:(dsc.get||null);});var INST=window.navigator;var VAL={};['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','getGamepads','javaEnabled','sendBeacon','vibrate','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){try{VAL[k]=INST[k];}catch(e){VAL[k]=undefined;}});['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','getGamepads','javaEnabled','sendBeacon','vibrate','constructor','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){try{delete NP[k];}catch(e){}});var mk=function(name,val){var f=function(){return val;};try{Object.defineProperty(f,\"name\",{value:\"get \"+name,configurable:true});}catch(e){}try{var nw=(globalThis.__lv8__&&globalThis.__lv8__.wrapNative)||(globalThis.__iv8__&&globalThis.__iv8__.__lv8_internal__&&globalThis.__iv8__.__lv8_internal__.wrapNative);if(nw)nw(f,\"get \"+name);}catch(e){}return f;};['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','getGamepads','javaEnabled','sendBeacon','vibrate','constructor','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){if(k===\"constructor\"){try{Object.defineProperty(NP,\"constructor\",{value:CAP.constructor||NP.constructor,writable:true,configurable:true,enumerable:false});}catch(e){}return;}if(['getGamepads','javaEnabled','sendBeacon','vibrate','constructor','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].indexOf(k)!==-1){var fn=(k===\"javaEnabled\"||k===\"getBattery\")&&CAP[k]?CAP[k]:VAL[k];if(typeof fn!==\"function\"){fn=function(){return undefined;};try{if(globalThis.__lv8__)globalThis.__lv8__.wrapNative(fn,k);}catch(e){}}try{Object.defineProperty(NP,k,{value:fn,writable:true,configurable:true,enumerable:true});}catch(e){}return;}var g=mk(k,VAL[k]);try{Object.defineProperty(NP,k,{get:g,set:undefined,configurable:true,enumerable:true});}catch(e){}});['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','getGamepads','javaEnabled','vibrate','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','deprecatedRunAdAuctionEnforcesKAnonymity'].forEach(function(k){if(k===\"sendBeacon\")return;try{delete INST[k];}catch(e){}});try{if(typeof VAL.sendBeacon===\"function\")Object.defineProperty(INST,\"sendBeacon\",{value:VAL.sendBeacon,writable:true,configurable:true,enumerable:true});}catch(e){}}}catch(e){}})();"
    # HTMLAnchorElement URL decomposition: the engine's href setter drops
    # the assigned value and the decomposed members read null.  The SDK's
    # window-binding probe parses the page URL through a throwaway anchor
    # and aborts when protocol/host come back null - two iframe probes and
    # their random draws never run.  Take over href storage (WeakMap of the
    # raw value) and define the decomposed members as parsers over it.
    + ANCHOR_URL_PARITY_JS
    # iframe contentWindow surface: the engine gives fresh iframe realms a
    # bare window (76 own keys vs the main window's 1135), so the SDK's
    # key-existence walk over a probe iframe collects a hole-ridden vector
    # and the pfd0 digest diverges.  Sync the main window's own descriptors
    # onto a contentWindow the first time it is read (realm-identity keys
    # already present on the bare window are left untouched).
    + "(function(){try{var IFP=window.HTMLIFrameElement&&HTMLIFrameElement.prototype;var CWD=IFP&&Object.getOwnPropertyDescriptor(IFP,'contentWindow');if(IFP&&CWD&&CWD.get){var __lv8Synced=new WeakSet();var __lv8SyncIframe=function(iw){if(!iw||__lv8Synced.has(iw))return iw;try{__lv8Synced.add(iw);}catch(e){return iw;}var names=[];try{names=Object.getOwnPropertyNames(window);}catch(e){return iw;}for(var i=0;i<names.length;i++){(function(k){try{if(k in iw)return;var d=Object.getOwnPropertyDescriptor(window,k);if(!d)return;if(d.get||d.set){Object.defineProperty(iw,k,{get:d.get,set:d.set,configurable:true,enumerable:d.enumerable});}else{Object.defineProperty(iw,k,{value:d.value,writable:d.writable!==false,configurable:true,enumerable:d.enumerable});}}catch(e){}})(names[i]);}return iw;};try{Object.defineProperty(IFP,'contentWindow',{get:function(){return __lv8SyncIframe(CWD.get.call(this));},set:CWD.set,configurable:CWD.configurable!==false,enumerable:CWD.enumerable});}catch(e){}}}catch(e){}})();"
    "try{const dp=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "if(dp)Object.getOwnPropertyNames(document).forEach(function(k){"
    "if(k==='location')return;"
    "var d=Object.getOwnPropertyDescriptor(document,k);"
    "if(!d||!d.configurable)return;delete document[k];"
    "if(!Object.prototype.hasOwnProperty.call(dp,k))"
    "Object.defineProperty(dp,k,d);});}catch(e){}"
    "})();"
    + FORM_ELEMENTS_INSTALL
    + INPUT_IDL_DEFAULTS_INSTALL
    + FORM_IDL_FACE_INSTALL
    + FACE_PARITY_INSTALL
    + FACE_ARITY_INSTALL
    + FACE_SHAPE_INSTALL
    + FUNCTION_NAME_SWEEP_INSTALL
    + FACE_ENUM_INSTALL
    + FACE_SHAPE_INSTALL
    # G4.393: wire every interface prototype into the reference chain
    # (lv8 builds most prototypes flat -> Object; the reference resolves
    # for-in/instanceof through the full webidl hierarchy).
    + INTERFACE_CHAIN_INSTALL_JS
    + WINDOW_NATIVE_FORM_JS
)


# The page-load prelude runs before the document-location binder and the
# bridge fix passes; any window own-key those passes re-lay is lost by the
# time page scripts run (the matchMedia wrapper was provably clobbered).
# POST_LOAD_PRELUDE therefore installs the surfaces that must outlive every
# fix pass but precede any page script (the SDK runs during drain()):
# the matchMedia query overrides (keys whitespace-normalized - the collector
# issues "(any-pointer: fine )"-style queries) plus the reference-measured shape
# fixes for the globals the challenge probes.
# G4.230 face normalize: enforce the reference-measured own-property faces
# for document / location / performance / style.  The binder chain and
# mid-page re-layouts re-lay these faces per load path, and the biz
# payload packs synchronously inside load_document - BEFORE the
# POST_LOAD prelude runs - so the engine also injects this block right
# after the document-location binder (G4.230).
# G4.230: the reference document face section from FACE_PARITY, extracted
# verbatim so it can also run at the binder (head-script) phase - the
# biz payload packs while page scripts run, before the post-load prelude.
FACE_DOC_FACE_JS = (r"""(function(){
  // ---- 2. document: Node link + Document/HTMLDocument face ----
  try {
    var doc = window.document;
    if (doc) {
      // G4.244: the host builds the reference chain (document ->
      // HTMLDocument.prototype -> Document.prototype -> Node.prototype);
      // this block only fills in the members the host surface still lacks.
      // Its target is pinned to the Document level below.
      // lv8's document chain is doc -> dp -> EventTarget.prototype (the
      // engine's Node global is a constants object, not a constructor, so
      // there is no Node.prototype to splice in).  The reference chain is
      // doc -> HTMLDocument.prototype -> Document.prototype ->
      // Node.prototype -> EventTarget.prototype; install the Node-level
      // members directly on the document proto level below.
      var dp2 = (function(){var _a=Object.getPrototypeOf(doc),_b=_a&&Object.getPrototypeOf(_a);
        return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();
      while (dp2 && !(dp2.constructor && dp2.constructor.name === 'Document') &&
             Object.getPrototypeOf(dp2) !== EventTarget.prototype &&
             Object.getPrototypeOf(dp2) !== Object.prototype) {
        dp2 = Object.getPrototypeOf(dp2);
      }
      // Pollution guard: an early realm state can expose the shared Node
      // hierarchy level as the document's proto; installing the Document
      // face there would leak it to every element (`clear` in body etc.).
      // Strip anything we planted on the Node level and only dress the
      // real document proto (which lacks the Node-level constants).
      try {
        var nl = Object.getPrototypeOf((document.body ||
          document.documentElement) || {});
        while (nl && nl.constructor && nl.constructor.name !== 'Node')
          nl = Object.getPrototypeOf(nl);
        if (nl && nl !== dp2) {
          var docOnly = ['clear', 'adoptNode', 'importNode', 'captureEvents',
            'releaseEvents', 'execCommand', 'getElementsByName',
            'createNodeIterator', 'createTreeWalker', 'elementFromPoint',
            'elementsFromPoint', 'caretPositionFromPoint',
            'caretRangeFromPoint', 'createAttribute', 'createAttributeNS',
            'createCDATASection', 'createComment', 'createNSResolver',
            'createProcessingInstruction', 'exitFullscreen',
            'exitPictureInPicture', 'exitPointerLock', 'queryCommandEnabled',
            'queryCommandIndeterm', 'queryCommandState',
            'queryCommandSupported', 'queryCommandValue',
            'getElementsByTagNameNS', 'prepend', 'append', 'moveBefore',
            'replaceChildren', 'bgColor', 'alinkColor', 'fgColor',
            'linkColor', 'vlinkColor', 'fullscreen',
            'pictureInPictureEnabled', 'webkitFullscreenEnabled',
            'webkitHidden', 'webkitIsFullScreen', 'xmlStandalone',
            'xmlVersion', 'fragmentDirective', 'rootElement',
            'webkitCurrentFullScreenElement', 'webkitFullscreenElement',
            'webkitVisibilityState'];
          docOnly.forEach(function (name) {
            if (Object.prototype.hasOwnProperty.call(nl, name)) {
              try { delete nl[name]; } catch (e31) {}
            }
          });
        }
      } catch (e32) {}
      if (dp2 && !Object.prototype.hasOwnProperty.call(dp2, 'ATTRIBUTE_NODE')) {
        function stubFn(name, length) {
          var f = function () { return null; };
          try { Object.defineProperty(f, 'name', { value: name }); } catch (e11) {}
          try { Object.defineProperty(f, 'length', { value: length }); } catch (e12) {}
          return f;
        }
        var fns = {
          adoptNode: 1, importNode: 1, clear: 0, captureEvents: 0,
          releaseEvents: 0, execCommand: 1, getElementsByName: 1,
          createNodeIterator: 1, createTreeWalker: 1, elementFromPoint: 0,
          elementsFromPoint: 2, caretPositionFromPoint: 2,
          caretRangeFromPoint: 2, createAttribute: 1, createAttributeNS: 2,
          createCDATASection: 1, createComment: 1, createNSResolver: 1,
          createProcessingInstruction: 2, exitFullscreen: 0,
          exitPictureInPicture: 0, exitPointerLock: 0,
          queryCommandEnabled: 1, queryCommandIndeterm: 1,
          queryCommandState: 1, queryCommandSupported: 1,
          queryCommandValue: 1, getElementsByTagNameNS: 2, prepend: 0,
          append: 0, moveBefore: 2, replaceChildren: 0
        };
        Object.keys(fns).forEach(function (name) {
          if (!Object.prototype.hasOwnProperty.call(dp2, name) &&
              !(name in dp2)) {
            try {
              Object.defineProperty(dp2, name, {
                value: stubFn(name, fns[name]),
                writable: true, enumerable: true, configurable: true });
            } catch (e13) {}
          }
        });
        // Node-level members (reference carries them via Node.prototype; the
        // reference document answers `in` for all of them)
        var nodeFns = {
          hasChildNodes: 0, appendChild: 1, insertBefore: 2,
          removeChild: 1, replaceChild: 2, cloneNode: 1, normalize: 0,
          contains: 1, compareDocumentPosition: 1, getRootNode: 0,
          isDefaultNamespace: 1, isEqualNode: 1, isSameNode: 1,
          lookupNamespaceURI: 1, lookupPrefix: 1
        };
        // G4.235: reference answers these through Node.prototype's NATIVE methods;
        // the business (second-load) document proto loses the Node link, and
        // the old fallback installed `function () { return null; }` stubs.
        // The SDK grabs document.appendChild into its VM table at boot and
        // later calls it; the stub's null return flipped the f21 state slot
        // (lv8 fed id 8 where reference fed 88 -> ESS fields 50/51 off by 1 bit).
        // Prefer the native implementation (Element/Node prototype) and only
        // fall back to the stub when no host implementation exists.
        var nativeNodeSrc = null;
        try {
          if (typeof Element === 'function' && Element.prototype)
            nativeNodeSrc = Element.prototype;
        } catch (e24a) {}
        if (!nativeNodeSrc) {
          try {
            if (typeof Node === 'function' && Node.prototype)
              nativeNodeSrc = Node.prototype;
          } catch (e24b) {}
        }
        Object.keys(nodeFns).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          var impl = null;
          try { impl = nativeNodeSrc ? nativeNodeSrc[name] : null; } catch (e24c) {}
          if (typeof impl !== 'function') impl = stubFn(name, nodeFns[name]);
          try {
            Object.defineProperty(dp2, name, {
              value: impl,
              writable: true, enumerable: true, configurable: true });
          } catch (e25) {}
        });
        var docNodeVals = { nodeType: 9, nodeName: '#document',
                            parentNode: null, parentElement: null,
                            ownerDocument: null, nextSibling: null,
                            previousSibling: null, isConnected: true,
                            textContent: null, baseURI: null };
        Object.keys(docNodeVals).forEach(function (key) {
          if (Object.prototype.hasOwnProperty.call(dp2, key) ||
              (key in dp2)) return;
          try {
            Object.defineProperty(dp2, key, {
              get: function () {
                if (key === 'baseURI') {
                  try { return String(document.location.href); }
                  catch (e26) { return 'about:blank'; }
                }
                if (key === 'isConnected') return true;
                return docNodeVals[key];
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e27) {}
        });
        // childNodes/firstChild/lastChild over the document element
        if (!Object.prototype.hasOwnProperty.call(dp2, 'childNodes') &&
            !('childNodes' in dp2)) {
          try {
            Object.defineProperty(dp2, 'childNodes', {
              get: function () {
                var out = [];
                try {
                  var kids = this.children;
                  if (kids && kids.length) {
                    for (var i = 0; i < kids.length; i++) out.push(kids[i]);
                  }
                } catch (e28) {}
                return out;
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e29) {}
        }
        ['firstChild', 'lastChild'].forEach(function (key) {
          if (Object.prototype.hasOwnProperty.call(dp2, key) ||
              (key in dp2)) return;
          try {
            Object.defineProperty(dp2, key, {
              get: function () {
                var kids = this.childNodes;
                if (!kids || !kids.length) return null;
                return key === 'firstChild' ? kids[0] : kids[kids.length - 1];
              },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e30) {}
        });
        function docColor(attr) {
          return {
            get: function () {
              var b = this.body || null;
              var v = null;
              try { v = b && b.getAttribute(attr); } catch (e14) {}
              return v === null || v === undefined ? '' : v;
            },
            set: function (val) {
              try {
                if (this.body) this.body.setAttribute(attr, String(val));
              } catch (e15) {}
            }
          };
        }
        var docAttrs = { bgColor: 'bgcolor', alinkColor: 'alink',
                         fgColor: 'fgcolor', linkColor: 'link',
                         vlinkColor: 'vlink' };
        Object.keys(docAttrs).forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key)) {
            var pair = docColor(docAttrs[key]);
            try {
              Object.defineProperty(dp2, key, {
                get: pair.get, set: pair.set,
                enumerable: true, configurable: true });
            } catch (e16) {}
          }
        });
        var docVals = { fullscreen: false, pictureInPictureEnabled: true,
                        webkitFullscreenEnabled: false,
                        webkitHidden: false, webkitIsFullScreen: false,
                        xmlStandalone: false, xmlVersion: '1.0' };
        Object.keys(docVals).forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key) &&
              !(key in dp2)) {
            try {
              Object.defineProperty(dp2, key, {
                get: function () { return docVals[key]; },
                set: function () {},
                enumerable: true, configurable: true });
            } catch (e17) {}
          }
        });
        var docNulls = ['fragmentDirective', 'rootElement',
                        'webkitCurrentFullScreenElement',
                        'webkitFullscreenElement'];
        docNulls.forEach(function (key) {
          if (!Object.prototype.hasOwnProperty.call(dp2, key) &&
              !(key in dp2)) {
            try {
              Object.defineProperty(dp2, key, {
                get: function () { return null; },
                set: function () {},
                enumerable: true, configurable: true });
            } catch (e18) {}
          }
        });
        if (!Object.prototype.hasOwnProperty.call(dp2, 'webkitVisibilityState') &&
            !('webkitVisibilityState' in dp2)) {
          try {
            Object.defineProperty(dp2, 'webkitVisibilityState', {
              get: function () { return 'visible'; },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e19) {}
        }
        // reference-only document members (for-in visible, enumerable)
        // G4.364: createExpression/evaluate left out - the return-null
        // stubs blocked the working XPath install (D1 fork root); the
        // xpath-family pass defines the real members on every document
        // chain instead.
        var docMoreFns = { createEvent: 1, open: 0, close: 0, hasFocus: 0 };
        Object.keys(docMoreFns).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          try {
            Object.defineProperty(dp2, name, {
              value: stubFn(name, docMoreFns[name]),
              writable: true, enumerable: true, configurable: true });
          } catch (e33) {}
        });
        if (!Object.prototype.hasOwnProperty.call(dp2, 'nodeValue') &&
            !('nodeValue' in dp2)) {
          try {
            Object.defineProperty(dp2, 'nodeValue', {
              get: function () { return null; },
              set: function () {},
              enumerable: true, configurable: true });
          } catch (e34) {}
        }
        var nodeConsts = { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2,
          TEXT_NODE: 3, CDATA_SECTION_NODE: 4, ENTITY_REFERENCE_NODE: 5,
          ENTITY_NODE: 6, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8,
          DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10,
          DOCUMENT_FRAGMENT_NODE: 11, NOTATION_NODE: 12,
          DOCUMENT_POSITION_DISCONNECTED: 1,
          DOCUMENT_POSITION_PRECEDING: 2,
          DOCUMENT_POSITION_FOLLOWING: 4,
          DOCUMENT_POSITION_CONTAINS: 8,
          DOCUMENT_POSITION_CONTAINED_BY: 16,
          DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 };
        Object.keys(nodeConsts).forEach(function (name) {
          if (Object.prototype.hasOwnProperty.call(dp2, name) ||
              (name in dp2)) return;
          try {
            Object.defineProperty(dp2, name, {
              value: nodeConsts[name],
              writable: false, enumerable: true, configurable: true });
          } catch (e35) {}
        });
        // referrer belongs to the Document interface level, not to the
        // document instance.  The per-load chain split below retains its
        // global-interface descriptor on Document.prototype.
      }
    }
  } catch (e) {}
})();""")


# G4.230: on*/scrollingElement/fonts proto installs, moved from
# POST_LOAD
DOCUMENT_WALK_FACE_JS = (
    # document event-handler face: reference's for-in(document) walks 116 on*
    # accessors on the document prototype (GlobalEventHandlers mixin in
    # Chrome spec order) - the challenge SDK walks exactly this list and
    # branches on each name's length before a regex .test, so the walk
    # order is engine-visible (G4.220: the ~27k read-stream transposition
    # came from lv8 walking only 3 names here).  Accessors store per
    # document instance (fresh handlers after a reload), default null.
    # document event-handler face: reference's for-in(document) walks 116 on*
    # accessors on the document prototype (GlobalEventHandlers mixin in
    # Chrome spec order) - the challenge SDK walks exactly this list and
    # branches on each name's length before a regex .test, so the walk
    # order is engine-visible (G4.220: the ~27k read-stream transposition
    # came from lv8 walking only 3 names here).  Accessors store per
    # document instance (fresh handlers after a reload), default null.
    "(function(){"
    "try{var dpp=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "['onmousemove','onselectionchange','onerror'].forEach(function(k){"
    "try{delete dpp[k];}catch(e){}"
    "});"
    "var dStore=new WeakMap();"
    "var dOn='" + pipe + "'.split('|');"
    "dOn.forEach(function(k){"
    "var g=function(){var m=dStore.get(this);return m&&m[k]||null;};"
    "var st=function(v){var m=dStore.get(this);"
    "if(!m){m={};dStore.set(this,m);}"
    "m[k]=typeof v==='function'?v:null;};"
    "try{Object.defineProperty(g,'name',{value:'get '+k,configurable:true});"
    "Object.defineProperty(st,'name',{value:'set '+k,configurable:true});}catch(e){}"
    "try{Object.defineProperty(dpp,k,{get:g,set:st,"
    "configurable:true,enumerable:true});}catch(e){}"
    "});"
    "}catch(e){}"
    # scrollingElement: the reference wheel's document has no element-scroller
    # binding and reads null.  Defined on the prototype layer post-relayout;
    # if any native scroll path breaks, this is the piece to revert.
    "try{var dpp2=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "if(dpp2){"
    "Object.defineProperty(dpp2,'scrollingElement',"
    "{get:function(){return null;},configurable:true,enumerable:true});}}"
    "catch(e){}"
    # fonts: reference's Document.prototype carries a get-only `fonts` accessor
    # reading null (no FontFaceSet).  lv8 lacked the key entirely, so the
    # page business core's font-measurement stage (`document.fonts !==
    # undefined` gate) skipped itself on lv8 - the missing font-enumeration
    # data diverged the page-signature signature and the WAF blocked every lv8
    # signed POST (G4.221).
    "try{var dpp3=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "if(dpp3&&!Object.prototype.hasOwnProperty.call(dpp3,'fonts')){"
    "var fng=function fonts(){return null;};"
    "try{if(globalThis.__lv8__&&globalThis.__lv8__.wrapNative)"
    "globalThis.__lv8__.wrapNative(fng,'get fonts');}catch(e){}"
    "Object.defineProperty(dpp3,'fonts',"
    "{get:fng,set:undefined,configurable:true,enumerable:true});}}"
    "catch(e){}"
    "})();"
)

# G4.235: reference exposes crypto.randomUUID as an anonymous native; the name
# inferred from the prototype key makes Function.prototype.toString render
# "function randomUUID() { [native code] }".  The SDK collects host function
# source strings as part of its environment fingerprint (first instruction
# divergence at tick 1109), so clear the name at the last prelude stage.
RU_NAME_FIX_JS = (
    "(function(){try{var P=window.Crypto&&Crypto.prototype;"
    "if(P){var d=Object.getOwnPropertyDescriptor(P,'randomUUID');"
    "if(d&&d.value&&typeof d.value==='function'&&d.value.name!=='')"
    "Object.defineProperty(d.value,'name',{value:'',configurable:true});}}"
    "catch(e){}})();"
)

FACE_NORMALIZE_JS = (
    "(function(){"
    # G4.230 face normalize: the binder chain and mid-page re-layouts
    # re-lay the document/location/performance own faces differently per
    # load path, and the offline A/B (probe_biz_reads count vector) shows
    # the surviving divergences the collector can see.  Restate the
    # reference-measured faces here - this prelude runs last before page
    # scripts, so the faces below are the ones page code observes.
    # document: own face = {location*gs, createElement*, referrergs}; no
    # own constructor / _styleSheets / getAnimations (prototype layer).
    "try{var dcc=Object.getOwnPropertyDescriptor(document,'constructor');"
    "if(dcc&&dcc.configurable){delete document.constructor;}}catch(e){}"
    "try{var dss=Object.getOwnPropertyDescriptor(document,'_styleSheets');"
    "if(dss&&dss.enumerable&&dss.configurable){"
    "Object.defineProperty(document,'_styleSheets',{value:dss.value,"
    "writable:true,enumerable:false,configurable:true});}}catch(e){}"
    "try{var dce=Object.getOwnPropertyDescriptor(document,'createElement');"
    "if(dce&&!dce.get&&!dce.enumerable&&dce.configurable){"
    "Object.defineProperty(document,'createElement',{value:dce.value,"
    "writable:dce.writable!==false,enumerable:true,configurable:true});}}"
    "catch(e){}"
    # G4.230/pathN: the reference window does NOT carry 'document' as an
    # own ENUMERABLE key (lv8 had it at position 1) - the SDK's key-walk
    # battery types every walked key, so the extra walk entry flips the
    # state tags (flag 85/88 parity -> block bytes 48/49).  Keep the
    # binding, hide it from enumeration.
    # G4.230/pathN: reference carries getComputedStyle as an OWN window key
    # right after getSelection; lv8 resolves it from a lower layer and
    # the walk sets diverge by exactly {document, getComputedStyle}.
    # Install it at the same own-slot (tail rebuilt in place).
    "try{var gcsF=window.getComputedStyle;"
    "if(typeof gcsF==='function'&&"
    "!Object.prototype.hasOwnProperty.call(window,'getComputedStyle')){"
    "var wOwn=Object.getOwnPropertyNames(window);"
    "var iSel=wOwn.indexOf('getSelection');"
    "if(iSel>=0){"
    "var tail=wOwn.slice(iSel+1);"
    "var tdesc={};"
    "tail.forEach(function(k){tdesc[k]="
    "Object.getOwnPropertyDescriptor(window,k);});"
    "tail.forEach(function(k){try{delete window[k];}catch(e){}});"
    "var newTail=['getComputedStyle'].concat(tail.filter(function(k){"
    "return k!=='getComputedStyle';}));"
    "newTail.forEach(function(k){var d=tdesc[k];"
    "if(!d){d={value:gcsF,writable:true,enumerable:true,"
    "configurable:true};}"
    "try{Object.defineProperty(window,k,d);}catch(e){}});"
    "}"
    "}"
    "}catch(e){}"
    "try{var dwc=Object.getOwnPropertyDescriptor(window,'document');"
    "if(dwc&&dwc.configurable&&dwc.enumerable){"
    "Object.defineProperty(window,'document',{get:dwc.get,set:dwc.set,"
    "enumerable:false,configurable:true});}}"
    "catch(e){}"
    "try{var dlc=Object.getOwnPropertyDescriptor(document,'location');"
    "if(dlc&&dlc.configurable){"
    "var dset=function(v){"
    "try{window.location.href=String(v);}catch(e){}};"
    "try{Object.defineProperty(dset,'name',"
    "{value:'set location',configurable:true});}catch(e0){}"
    "Object.defineProperty(document,'location',"
    "{get:dlc.get,set:dset,enumerable:true,configurable:true});}}"
    "catch(e){}"
    # location: 15 own members, valueOf first (non-enumerable), and the
    # protocol/host/hostname accessors carry setters (reference *gs faces; the
    # SDK enumerates location keys and regex-tests each name).
    "try{var loc=window.location;"
    "if(loc){"
    "var lOrder=['valueOf','ancestorOrigins','href','origin','protocol',"
    "'host','hostname','port','pathname','search','hash','assign',"
    "'reload','replace','toString'];"
    "var lp=Object.getPrototypeOf(loc);"
    "var lSnap={};"
    "lOrder.forEach(function(k){"
    "var d=Object.getOwnPropertyDescriptor(loc,k)||"
    "Object.getOwnPropertyDescriptor(lp,k);"
    "if(d)lSnap[k]=d;});"
    "var lMkset=function(kind){return function(v){"
    "try{var h=String(loc.href||'');var s=String(v);"
    "if(kind==='protocol'){"
    "h=h.replace(/^[a-zA-Z][a-zA-Z0-9+.-]*:/,"
    "s.replace(/:$/,'')+':');}"
    "else if(kind==='host'){"
    "h=h.replace(/^([a-zA-Z][a-zA-Z0-9+.-]*:\\/\\/)([^/?#]*)/,"
    "'$1'+s);}"
    "else{"
    "h=h.replace(/^([a-zA-Z][a-zA-Z0-9+.-]*:\\/\\/)([^/?#:]*)/,"
    "'$1'+s);}"
    "loc.href=h;}catch(e){}};};"
    "var lNeedSet={protocol:1,host:1,hostname:1,port:1};"
    "var lOK=true;"
    "var lRebuilt=lOrder.map(function(k){"
    "var d=lSnap[k];"
    "if(!d){lOK=false;return null;}"
    "if(k==='valueOf'){"
    "return [k,{value:function valueOf(){return this;},"
    "writable:true,enumerable:false,configurable:true}];}"
    "if(d.get||d.set){"
    "return [k,{get:d.get,"
    "set:(d.set?d.set:(lNeedSet[k]?lMkset(k):undefined)),"
    "enumerable:true,configurable:true}];}"
    "return [k,{value:d.value,writable:d.writable!==false,"
    "enumerable:true,configurable:true}];"
    "});"
    "if(lOK){"
    "lOrder.forEach(function(k){try{delete loc[k];}catch(e){}});"
    "lRebuilt.forEach(function(p){"
    "try{Object.defineProperty(loc,p[0],p[1]);}catch(e){}});"
    "}"
    "}"
    "}catch(e){}"
    # performance: reference's instance carries NO own members - the 20-member
    # face lives on the prototype layer in this exact order (probe A/B:
    # lv8 missed onresourcetimingbufferfull + dispatchEvent on the biz
    # load path).  Collapse any instance-own copies onto the prototype.
    "try{var pFixedSet=new WeakSet;"
    "var pFix=function(perf){"
    "if(!perf||pFixedSet.has(perf))return;try{pFixedSet.add(perf);}"
    "catch(e9){}"
    "var pOrder=['timeOrigin','onresourcetimingbufferfull','clearMarks',"
    "'clearMeasures','clearResourceTimings','getEntries',"
    "'getEntriesByName','getEntriesByType','mark','measure','now',"
    "'setResourceTimingBufferSize','toJSON','timing','navigation',"
    "'memory','eventCounts','addEventListener','removeEventListener',"
    "'dispatchEvent'];"
    "var pp=Object.getPrototypeOf(perf);"
    "if(pp){"
    "var pSnap={};"
    "pOrder.forEach(function(k){"
    "var d=Object.getOwnPropertyDescriptor(perf,k)||"
    "Object.getOwnPropertyDescriptor(pp,k);"
    "if(d)pSnap[k]=d;});"
    "pOrder.forEach(function(k){"
    "try{delete perf[k];}catch(e){}"
    "try{delete pp[k];}catch(e){}});"
    "pOrder.forEach(function(k){var d=pSnap[k];"
    "if(!d){"
    "if(k==='dispatchEvent'&&window.EventTarget&&"
    "EventTarget.prototype){"
    "d=Object.getOwnPropertyDescriptor(EventTarget.prototype,"
    "'dispatchEvent');}"
    "else if(k==='onresourcetimingbufferfull'){"
    "var hv=null;"
    "var pg=function(){return hv;};"
    "var ps=function(v){hv=typeof v==='function'?v:null;};"
    "try{Object.defineProperty(pg,'name',"
    "{value:'get '+k,configurable:true});}catch(e1){}"
    "try{Object.defineProperty(ps,'name',"
    "{value:'set '+k,configurable:true});}catch(e2){}"
    "d={get:pg,set:ps,enumerable:true,configurable:true};}"
    "else{"
    "var stub=function(){return undefined;};"
    "try{Object.defineProperty(stub,'name',"
    "{value:k,configurable:true});}catch(e3){}"
    "d={value:stub,writable:true,enumerable:true,"
    "configurable:true};}}"
    "if(d)try{Object.defineProperty(pp,k,d);}catch(e4){}});"
    "}"
    "};"
    "var pf=window.performance;"
    "if(pf){try{pFix(pf);}catch(e5){}}"
    "else{"
    "var pfd=Object.getOwnPropertyDescriptor(window,'performance');"
    "if(pfd&&pfd.configurable){"
    "var pDone=false;"
    "var pGet=function(){"
    "var v=pfd.get?pfd.get.call(this):pfd.value;"
    "if(!pDone&&v){pDone=true;try{pFix(v);}catch(e6){}}"
    "return v;};"
    "try{Object.defineProperty(window,'performance',"
    "{get:pGet,set:pfd.set,configurable:true,"
    "enumerable:pfd.enumerable!==false});}catch(e7){}"
    "}"
    "}"
    "}catch(e){}"
    # G4.230: six Document members reference answers but lv8 lacked at pack
    # time (document walk for-in: 285 vs 284); hasFocus is a classic
    # boolean probe, the rest close the walk-name set.
    "try{var dpp4=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "var mk6=function(n,l,f){"
    "if(Object.prototype.hasOwnProperty.call(dpp4,n)||(n in dpp4))return;"
    "var fn=f||function(){return null;};"
    "try{Object.defineProperty(fn,'name',{value:n,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(fn,'length',{value:l,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(dpp4,n,{value:fn,writable:true,"
    "enumerable:true,configurable:true});}catch(e){}};"
    "mk6('hasFocus',0,function(){return true;});"
    "mk6('open',0,function(){return document;});"
    "mk6('close',0,function(){});"
    "mk6('createEvent',1);"
    "}catch(e){}"
    # G4.230: the business bundle's babel/core-js layer installs nine
    # enumerable window globals on lv8 (missing-engine features) that the
    # reference engine answers natively and never exposes.  The SDK's
    # canary scan walks window keys, so drop the nine right before the
    # payload (first big eval) runs.
    # G4.232: window.frames is the current window itself in reference
    # (frames === window, get+set accessor); the G4.230 live frame list
    # answered an Array, so the SDK's value-face battery typed it
    # 'Array' (see docs/roadmap doc §3 线索 A).  window.length keeps the
    # browser semantic (number of child frames = attached iframe/frame
    # elements).
    "try{var lv8CountFrames=function(){"
    "var n=0;try{n=document.querySelectorAll('iframe,frame').length;}"
    "catch(e){}return n;};"
    "Object.defineProperty(window,'frames',{get:function(){return window;},"
    "set:function(v){},configurable:true,enumerable:true});"
    "Object.defineProperty(window,'length',{get:function(){"
    "return lv8CountFrames();},set:function(v){},"
    "configurable:true,enumerable:true});"
    "}catch(e){}"
    # G4.230: pre-define the babel/core-js globals NON-ENUMERABLY - the
    # bundle's feature checks (`in`) then skip their enumerable installs,
    # and the SDK's for-in key scan cannot see them (reference parity: the
    # scan list there never contains these names).
    "try{var c9d={core:{},global:window,System:{},"
    "asap:function(f){return setTimeout(f,0);},"
    "Observable:function(){},regeneratorRuntime:{},"
    "setImmediate:function(f){var r=Array.prototype.slice.call(arguments,1);"
    "return setTimeout(function(){f.apply(null,r);},0);},"
    "clearImmediate:function(id){return clearTimeout(id);},"
    "_babelPolyfill:undefined};"
    "var c9Keys=Object.keys(c9d);"
    "for(var ci=0;ci<c9Keys.length;ci++){"
    "(function(k){try{"
    "Object.defineProperty(window,k,{value:c9d[k],writable:true,"
    "enumerable:false,configurable:true});}catch(e){}})(c9Keys[ci]);}"
    "}catch(e){}"
    "try{var oE=window.eval;"
    "var c9=['core','global','System','asap','Observable',"
    "'setImmediate','clearImmediate','regeneratorRuntime',"
    "'_babelPolyfill'];"
    # M3/G4.308: the canary state used to live on the window face under
    # Symbol.for('lv8c9dbg') - an own symbol the reference's symbol scans
    # observe (read-order 132531).  Closure-local + internal channel only.
    "var c9S={dcl:0,eval:0,present:-1,tmo:0};"
    "try{var c9I=globalThis.__iv8__&&globalThis.__iv8__.__lv8_internal__;"
    "if(c9I)c9I.c9=c9S;}catch(e){}"
    "var c9run=function(){var n=0;"
    "c9.forEach(function(k){if(Object.prototype.hasOwnProperty.call(window,k)){n++;try{delete window[k];}catch(e){}}});"
    "return n;};"
    "try{document.addEventListener('DOMContentLoaded',function(){"
    "c9S.dcl=c9run();});}catch(e){}"
    "try{setTimeout(function(){c9S.tmo=c9run();},0);}catch(e){}"
    "window.eval=function(s){"
    "try{if(typeof s==='string'&&s.length>300000&&"
    "s.indexOf('function _$hK(_$kJ,_$gT){')>=0){"
    "c9S.eval=c9run();}}"
    "catch(e){}"
    "return oE.apply(this,arguments);};"
    "}catch(e){}"
    # CSSStyleDeclaration: the 347-name interceptor install marks its work
    # with an enumerable own __lv8CssInNames on the prototype - reference's
    # style for-in enumerates exactly the 9 declaration members, so hide
    # the marker (G4.230 style for-in was 10 vs 9).
    "try{var bs=(document.body||document.documentElement);"
    "if(bs&&bs.style){var stp=Object.getPrototypeOf(bs.style);"
    "var cmd=Object.getOwnPropertyDescriptor(stp,'__lv8CssInNames');"
    "if(cmd&&cmd.enumerable){"
    "Object.defineProperty(stp,'__lv8CssInNames',{value:cmd.value,"
    "writable:true,enumerable:false,configurable:true});}}}"
    "catch(e){}"
    "})();"
+ DOCUMENT_WALK_FACE_JS + FACE_DOC_FACE_JS + RU_NAME_FIX_JS
)

# G4.232 value-face brand: reference exposes the five window slots below as
# branded interface instances (own keys [], members on the interface
# prototype, constructor.name = interface name).  lv8 left
# customElements/trustedTypes/scheduler as plain objects with own
# members and Object as constructor; the SDK's value-face battery types
# window.customElements etc. and can branch on the result.  This pass
# re-homes the members onto a branded prototype and restores the
# document constructor name (reference: HTMLDocument).
VALUE_BRAND_FACE_JS = (
    "(function(){"
    "try{"
    "var BRAND=function(obj,iface,members){"
    "try{"
    "if(!obj||typeof obj!=='object')return;"
    "var proto=Object.getPrototypeOf(obj);"
    "if(proto&&proto.constructor&&proto.constructor.name===iface)return;"
    "var P={};"
    "var C=function(){throw new TypeError('Illegal constructor');};"
    "try{Object.defineProperty(C,'name',{value:iface,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(C,'length',{value:0,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(P,'constructor',{value:C,writable:true,"
    "configurable:true,enumerable:false});}catch(e){}"
    "try{Object.defineProperty(P,Symbol.toStringTag,"
    "{value:iface,configurable:true});}catch(e){}"
    "try{if(proto&&proto!==Object.prototype){"
    "Object.getOwnPropertyNames(proto).forEach(function(k){"
    "if(k==='constructor')return;"
    "var d=Object.getOwnPropertyDescriptor(proto,k);"
    "try{if(!Object.prototype.hasOwnProperty.call(P,k))"
    "Object.defineProperty(P,k,d);}catch(e){}});}}catch(e){}"
    "try{Object.getOwnPropertyNames(obj).forEach(function(k){"
    "if(k==='constructor')return;"
    "var d=Object.getOwnPropertyDescriptor(obj,k);"
    "try{delete obj[k];}catch(e){}"
    "try{Object.defineProperty(P,k,d);}catch(e){}});}catch(e){}"
    "members.forEach(function(m){"
    "if(Object.prototype.hasOwnProperty.call(P,m))return;"
    "var f=function(){return null;};"
    "try{Object.defineProperty(f,'name',{value:m,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(f,'length',{value:0,configurable:true});}"
    "catch(e){}"
    "try{Object.defineProperty(P,m,{value:f,writable:true,"
    "configurable:true,enumerable:true});}catch(e){}});"
    "try{Object.setPrototypeOf(obj,P);}catch(e){}"
    "}catch(e){}};"
    "BRAND(window.customElements,'CustomElementRegistry',"
    "['define','get','getName','whenDefined','upgrade']);"
    "BRAND(window.external,'External',"
    "['AddSearchProvider','IsSearchProviderInstalled']);"
    "BRAND(window.trustedTypes,'TrustedTypePolicyFactory',"
    "['emptyHTML','emptyScript','defaultPolicy','createPolicy',"
    "'getAttributeType','getPropertyType','getTypeMapping','isHTML',"
    "'isScript','isScriptURL']);"
    "BRAND(window.scheduler,'Scheduler',['postTask']);"
    "BRAND(window.visualViewport,'VisualViewport',"
    "['offsetLeft','offsetTop','pageLeft','pageTop','width','height',"
    "'scale','onresize','onscroll']);"
    "try{var dp=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "var dc=dp&&dp.constructor;"
    "if(dc&&typeof dc==='function'&&dc.name!=='HTMLDocument'){"
    "Object.defineProperty(dc,'name',{value:'HTMLDocument',"
    "configurable:true});}}catch(e){}"
    "}catch(e){}"
    # M5: native load_document re-lays EventTarget.prototype with its own
    # member order (constructor first); the reference lists the event trio
    # first and the constructor last (observable on every event-target
    # chain via gOPN/for-in).
    "try{var etp=(typeof EventTarget==='function')?EventTarget.prototype:null;"
    "if(etp){var ecd=Object.getOwnPropertyDescriptor(etp,'constructor');"
    "if(ecd&&ecd.configurable){delete etp.constructor;"
    "Object.defineProperty(etp,'constructor',ecd);}}}catch(e){}"
    "})();"
)

# The value-face brand is appended after all face constants are defined
# (PAGE_LOAD_PRELUDE itself is imported after this module finishes).
PAGE_LOAD_PRELUDE += VALUE_BRAND_FACE_JS

# G4.245: the reference wheel keeps HTMLDocument.prototype's own face at
# `constructor` only - every document member (accessors, on* handlers,
# methods) lives on Document.prototype.  Prelude/polyfill installers may
# still target the first prototype object, so anything they planted there is
# moved down to the Document level at the end of the load.
DOCUMENT_LEVEL_MOVE_JS = ("""
(function () {
  try {
    var hd = Object.getPrototypeOf(document);
    if (!hd) return;
    var dp = Object.getPrototypeOf(hd);
    if (!dp) return;
    if (!(hd.constructor && hd.constructor.name === 'HTMLDocument')) return;
    if (!(dp.constructor && dp.constructor.name === 'Document')) return;
    var names = Object.getOwnPropertyNames(hd);
    for (var i = 0; i < names.length; i++) {
      var k = names[i];
      if (k === 'constructor') continue;
      var dsc = null;
      try { dsc = Object.getOwnPropertyDescriptor(hd, k); } catch (e) { continue; }
      if (!dsc) continue;
      if (!Object.prototype.hasOwnProperty.call(dp, k)) {
        try { Object.defineProperty(dp, k, dsc); } catch (e2) {}
      }
      try { delete hd[k]; } catch (e3) {}
    }
  } catch (e) {}
})();
""")


POST_LOAD_PRELUDE = (
    "(function(){"
    # document.getAnimations: the reference resolves it callable on the
    # live document but carries NO own key for it (G4.230 face dump) - host
    # it on the prototype layer so the instance own-face stays reference-clean.
    "try{var ab=globalThis.__iv8__&&globalThis.__iv8__.__lv8_internal__;"
    "if(ab&&typeof ab.getAnimations==='function'&&"
    "typeof document.getAnimations!=='function'){"
    "var dga_p=(function(){var _a=Object.getPrototypeOf(document),_b=_a&&Object.getPrototypeOf(_a);return (_a&&_a.constructor&&_a.constructor.name==='HTMLDocument'&&_b&&_b.constructor&&_b.constructor.name==='Document')?_b:_a;})();"
    "if(dga_p)Object.defineProperty(dga_p,'getAnimations',"
    "{value:ab.getAnimations,writable:true,configurable:true,enumerable:true});}"
    "}catch(e){}"
    # matchMedia: the host itself now answers the reference wheel's feature
    # set (G4.238, native EvalMediaQuery: pointer/any-pointer/hover/
    # any-hover/color-gamut/display-mode/prefers-*), so no JS override is
    # re-hosted here - the SDK captures the native function into its VM
    # table at boot and would bypass any window-level wrapper anyway.
    # function shapes the collector's feature probes observe
    "try{if(typeof CloseEvent==='function'&&CloseEvent.length===0){"
    "Object.defineProperty(CloseEvent,'length',{value:1,configurable:true});}}"
    "catch(e){}"
    "try{if(typeof Worker==='function'&&Worker.length===0){"
    "Object.defineProperty(Worker,'length',{value:1,configurable:true});}}"
    "catch(e){}"
    "try{if(typeof webkitRequestFileSystem==='function'&&"
    "webkitRequestFileSystem.length===0){"
    "Object.defineProperty(webkitRequestFileSystem,'length',"
    "{value:3,configurable:true});}}catch(e){}"
    "try{if(window.MediaSource&&"
    "typeof window.MediaSource.isTypeSupported==='function'&&"
    "window.MediaSource.isTypeSupported.length===0){"
    "Object.defineProperty(window.MediaSource.isTypeSupported,'length',"
    "{value:1,configurable:true});}}catch(e){}"
    "try{if(typeof webkitMediaStream==='function'&&"
    "webkitMediaStream.name==='webkitMediaStream'){"
    "Object.defineProperty(webkitMediaStream,'name',"
    "{value:'MediaStream',configurable:true});}}catch(e){}"
    "try{if(typeof IdleDetector==='function'&&"
    "IdleDetector.name!=='IdleDetector'){"
    "Object.defineProperty(IdleDetector,'name',"
    "{value:'IdleDetector',configurable:true});}}catch(e){}"
    # chrome own-key order: reference shows loadTimes,csi,app
    "try{if(window.chrome){"
    "var ck=['loadTimes','csi','app'];"
    "var cds=ck.map(function(k){"
    "return Object.getOwnPropertyDescriptor(window.chrome,k);});"
    "var ok=cds.every(function(d){return d&&d.configurable;});"
    "if(ok){"
    "ck.forEach(function(k){delete window.chrome[k];});"
    "ck.forEach(function(k,i){"
    "Object.defineProperty(window.chrome,k,cds[i]);});"
    "}}"
    "}catch(e){}"
            # external: reference shows no OWN keys ([object External]{}).  The native
    # prototype chain is Object.prototype directly, so re-home the install
    # onto a fresh prototype object (keeps the accessor reachable without
    # polluting Object.prototype).
    "try{if(window.external&&Object.prototype.hasOwnProperty.call("
    "window.external,'IsSearchProviderInstalled')){"
    "var ex=window.external;"
    "var ed=Object.getOwnPropertyDescriptor(ex,'IsSearchProviderInstalled');"
    "var np={};"
    "delete ex.IsSearchProviderInstalled;"
    "Object.setPrototypeOf(ex,np);"
    "Object.defineProperty(np,'IsSearchProviderInstalled',ed);"
    "}}catch(e){}"
# speechSynthesis: reference keeps no own keys either
    "try{if(window.speechSynthesis&&"
    "typeof window.speechSynthesis==='object'){"
    "var ss=window.speechSynthesis;"
    "var ssp=Object.getPrototypeOf(ss);"
    "if(ssp&&ssp!==Object.prototype){"
    "['pending','speaking','paused','voices'].forEach(function(k){"
    "if(!Object.prototype.hasOwnProperty.call(ss,k))return;"
    "var d=Object.getOwnPropertyDescriptor(ss,k);"
    "if(Object.prototype.hasOwnProperty.call(ssp,k))return;"
    "try{delete ss[k];Object.defineProperty(ssp,k,d);}catch(e){}"
    "});"
    "}"
    "}"
    "}catch(e){}"
    # CSS support probes: reference's style object satisfies the collector's
    # feature checks natively; lv8's engine does not expose these four
    # names to the `in` operator, so hang real prototype accessors.
    # Deliberate engine-difference compensation - costs a 4-key proto P3
    # in surface-audit, buys 4 bitfield bits.
    "try{var SCP=window.CSSStyleDeclaration&&CSSStyleDeclaration.prototype;"
    "if(SCP){['backgroundBlendMode','lineBreak','textAlignLast','minWidth']"
    ".forEach(function(p){"
    "if(Object.prototype.hasOwnProperty.call(SCP,p))return;"
    "Object.defineProperty(SCP,p,{get:function(){return '';},"
    "set:function(v){},configurable:true,enumerable:true});});}}"
    "catch(e){}"
    # load_document re-installs the native broken toGMTString (bare
    # "H:MM:SS AM"); re-apply the toUTCString alias after every relayout.
    "try{var gp=new Date(86400000);if(String(gp.toGMTString())!==String(gp.toUTCString())){var g=function toGMTString(){return this.toUTCString();};try{Object.defineProperty(g,'length',{value:0,configurable:true});}catch(e){}try{if(globalThis.__lv8__&&globalThis.__lv8__.wrapNative)globalThis.__lv8__.wrapNative(g,'toGMTString');}catch(e){}Object.defineProperty(Date.prototype,'toGMTString',{value:g,writable:true,configurable:true,enumerable:false});}}catch(e){};"
    "try{delete window.defaultStatus;}catch(e){}"
    "})();"
) + FACE_NORMALIZE_JS + DOCUMENT_LEVEL_MOVE_JS + FACE_ENUM_INSTALL + FACE_SHAPE_INSTALL + ELEMENT_STYLE_RO_JS + FACE_VALUE_SHAPE_JS + SMALL_SET_GAPS_JS + MEDIA_ELEMENT_FACE_JS

REF_CTOR_ARGREQ_JS = """
(function () {
  var MSG = %s;
  var KEYS = Object.keys(MSG);
  for (var i = 0; i < KEYS.length; i++) (function (name) {
    var F = null;
    try { F = window[name]; } catch (e0) { return; }
    if (typeof F !== 'function' || !F.prototype) return;
    // idempotent + stub-only: wrap while the ctor still answers the bare
    // "Illegal constructor" stub message; skip once aligned
    if (F.__lv8ArgreqWrap) return;
    var g = function () { 'use strict';
      throw new TypeError(MSG[name]); };
    try {
      var cd = Object.getOwnPropertyDescriptor(F.prototype, 'constructor');
      Object.defineProperty(F.prototype, 'constructor',
        { value: g, writable: cd ? !!cd.writable : true,
          enumerable: cd ? !!cd.enumerable : false,
          configurable: cd ? !!cd.configurable : true });
    } catch (ec) {}
    try { Object.defineProperty(g, 'name',
          { value: name, configurable: true }); } catch (e2) {}
    try { Object.defineProperty(g, 'length',
          { value: F.length, configurable: true }); } catch (e3) {}
    try { Object.defineProperty(g, '__lv8ArgreqWrap',
          { value: true, configurable: true }); } catch (e3b) {}
    try { g.prototype = F.prototype; } catch (e4) {}
    try { Object.setPrototypeOf(g, F); } catch (e5) {}
    try {
      var nw = (globalThis.__lv8__ && globalThis.__lv8__.wrapNative) ||
               (globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__ &&
                globalThis.__iv8__.__lv8_internal__.wrapNative);
      if (nw) nw(g, name);
    } catch (e6) {}
    try {
      var cur = Object.getOwnPropertyDescriptor(window, name);
      Object.defineProperty(window, name,
        { value: g, writable: true, configurable: true,
          enumerable: cur ? !!cur.enumerable : false });
    } catch (e7) {}
  })(KEYS[i]);
})();
"""
REF_CTOR_ARGREQ_TABLE_JSON = _json.dumps(_REF_CTOR_ARGREQ, ensure_ascii=False)
REF_CTOR_ARGREQ_JS = REF_CTOR_ARGREQ_JS.replace('%s', REF_CTOR_ARGREQ_TABLE_JSON, 1)

REF_CTOR_ZEROARG_JS = """
(function () {
  var TABLE = %s;
  var KEYS = Object.keys(TABLE);
  for (var i = 0; i < KEYS.length; i++) (function (name) {
    var mode = TABLE[name][0], msg = TABLE[name][1];
    var F = null;
    try { F = window[name]; } catch (e0) { return; }
    if (typeof F !== 'function' || !F.prototype) return;
    // idempotent via marker - NEVER probe-construct: these are live
    // constructors with side effects (a 0-arg new WebSocket() during the
    // probe logged a ghost WEBSOCKET netLog entry, G4.323)
    if (F.__lv8ZeroargWrap) return;
    var g = function () { 'use strict';
      // validate: 0-arg refuses; real calls pass through.  illegal: a
      // DIRECT new (new.target === g) refuses; a subclass super()
      // (new.target = the subclass) passes through so `class X extends
      // HTMLElement` keeps working.  new.target g/undefined normalize to F
      // so the C++ ctor allocates from its own InstanceTemplate (a bare
      // this breaks ctors that read internal fields)
      var nt = (new.target === undefined || new.target === g)
        ? F : new.target;
      if (mode === 'validate') {
        if (arguments.length === 0) throw new TypeError(msg);
        return Reflect.construct(F, arguments, nt);
      }
      if (new.target === undefined || new.target === g)
        throw new TypeError(msg);
      return Reflect.construct(F, arguments, nt);
    };
    try {
      var cd = Object.getOwnPropertyDescriptor(F.prototype, 'constructor');
      Object.defineProperty(F.prototype, 'constructor',
        { value: g, writable: cd ? !!cd.writable : true,
          enumerable: cd ? !!cd.enumerable : false,
          configurable: cd ? !!cd.configurable : true });
    } catch (ec) {}
    try { Object.defineProperty(g, 'name',
          { value: name, configurable: true }); } catch (e2) {}
    try { Object.defineProperty(g, 'length',
          { value: F.length, configurable: true }); } catch (e3) {}
    try { Object.defineProperty(g, '__lv8ZeroargWrap',
          { value: true, configurable: true }); } catch (e3b) {}
    try { g.prototype = F.prototype; } catch (e4) {}
    try { Object.setPrototypeOf(g, F); } catch (e5) {}
    try {
      var nw = (globalThis.__lv8__ && globalThis.__lv8__.wrapNative) ||
               (globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__ &&
                globalThis.__iv8__.__lv8_internal__.wrapNative);
      if (nw) nw(g, name);
    } catch (e6) {}
    try {
      var cur = Object.getOwnPropertyDescriptor(window, name);
      Object.defineProperty(window, name,
        { value: g, writable: true, configurable: true,
          enumerable: cur ? !!cur.enumerable : false });
    } catch (e7) {}
  })(KEYS[i]);
})();
""" % _json.dumps(_REF_CTOR_ZEROARG, ensure_ascii=False)

REF_CTOR_THIN_JS = """
(function () {
  var TABLE = %s;
  // per-name construction hooks (M17/M19): live state factories for thin
  // wrappers - MediaStream/webkitMediaStream carry a per-instance UUID
  var THIN_INIT = (function () {
    var ids = new WeakMap();
    function uuid() {
      try { if (typeof crypto !== 'undefined' && crypto.randomUUID)
        return crypto.randomUUID(); } catch (e0) {}
      return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(
        /[xy]/g, function (c) {
          var r = Math.random() * 16 | 0;
          return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
        });
    }
    function initMediaStream() { ids.set(this, { id: uuid() }); }
    function installIdGetter(proto) {
      var d = null;
      try { d = Object.getOwnPropertyDescriptor(proto, 'id'); } catch (e1) {}
      if (d && !d.configurable) return;
      var get = function () {
        var st = ids.get(this);
        return st ? st.id : undefined;
      };
      try { Object.defineProperty(get, 'name',
            { value: 'get id', configurable: true }); } catch (e2) {}
      var desc = { get: get, enumerable: true, configurable: true };
      if (d && d.set) desc.set = d.set;
      try { Object.defineProperty(proto, 'id', desc); } catch (e3) {}
    }
    return {
      MediaStream: initMediaStream,
      webkitMediaStream: initMediaStream,
      afterInstall: function (name, proto) {
        if (name === 'MediaStream' || name === 'webkitMediaStream')
          installIdGetter(proto);
      }
    };
  })();
  var KEYS = Object.keys(TABLE);
  for (var i = 0; i < KEYS.length; i++) (function (name) {
    var tagOverride = TABLE[name];
    var F = null;
    try { F = window[name]; } catch (e0) { return; }
    if (typeof F !== 'function' || !F.prototype) return;
    if (F.__lv8ThinWrap) return;
    var g = function () { 'use strict';
      // reference parity (M15): `new X()` yields a zero-own shell whose tag is
      // the interface (or the canonical name for alias ctors)
      var o = Object.create(F.prototype);
      try { if (THIN_INIT[name]) THIN_INIT[name].call(o); } catch (e9) {}
      return o;
    };
    // G4.383: alias ctors carry the canonical brand as their name
    // (WebKitCSSMatrix -> "DOMMatrix", webkit* -> unprefixed), the
    // reference's Function#name for the same global.
    try { Object.defineProperty(g, 'name',
          { value: tagOverride || name, configurable: true }); } catch (e1) {}
    try { Object.defineProperty(g, 'length',
          { value: F.length, configurable: true }); } catch (e2) {}
    try { Object.defineProperty(g, '__lv8ThinWrap',
          { value: true, configurable: true }); } catch (e2b) {}
    try { g.prototype = F.prototype; } catch (e3) {}
    try {
      var want = tagOverride || name;
      var cur = Object.getOwnPropertyDescriptor(F.prototype,
                                               Symbol.toStringTag);
      if (!cur || cur.value !== want) {
        try { delete F.prototype[Symbol.toStringTag]; } catch (e4) {}
        Object.defineProperty(F.prototype, Symbol.toStringTag,
          { value: want, writable: false, enumerable: false,
            configurable: true });
      }
    } catch (e5) {}
    try { Object.setPrototypeOf(g, F); } catch (e6) {}
    try {
      var nw = (globalThis.__lv8__ && globalThis.__lv8__.wrapNative) ||
               (globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__ &&
                globalThis.__iv8__.__lv8_internal__.wrapNative);
      if (nw) nw(g, tagOverride || name);
    } catch (e7) {}
    try { THIN_INIT.afterInstall(name, F.prototype); } catch (e7b) {}
    try {
      var curD = Object.getOwnPropertyDescriptor(window, name);
      Object.defineProperty(window, name,
        { value: g, writable: true, configurable: true,
          enumerable: curD ? !!curD.enumerable : false });
    } catch (e8) {}
  })(KEYS[i]);
})();
""" % _json.dumps(_REF_CTOR_THIN, ensure_ascii=False)


POST_LOAD_PRELUDE = POST_LOAD_PRELUDE + REF_CTOR_ARGREQ_JS + REF_CTOR_ZEROARG_JS + REF_CTOR_THIN_JS
# Face normalizers in POST_LOAD_PRELUDE overwrite several Document members.
# Split and re-home the live document chain only after every such pass.
POST_LOAD_PRELUDE += DOCUMENT_CHAIN_TWO_LEVEL_WIRE_JS

__all__ = ["PAGE_LOAD_PRELUDE", "POST_LOAD_PRELUDE",
           "REALM_RESET_PRELUDE",
           "NAVIGATOR_SURFACE_JS", "COOKIE_JAR_PARITY_JS"]

# reference parity: a navigation lands on a fresh realm - page-added globals are
# gone and host prototypes carry no page-installed hooks.  lv8 is a single
# context, so a reload must undo the previous page's JS-visible mutations
# itself.  The challenge SDK's business-page core re-wraps XHR.open/send on
# reload; underneath it then sits the CHALLENGE stage's stale wrapper whose
# guard rejects the new page and swallows the open (readyState stuck at 0,
# no signed request) - the root cause of the login-tool ess miss (G4.221).
# On the first load this prelude snapshots the global face plus the bridge
# prototypes a captured SDK can hook; on every later load it restores
# replaced/deleted baseline members and deletes page-added configurable
# globals.  The snapshot lives in a non-enumerable slot on the __iv8__
# bridge so page scripts cannot enumerate or trip over it.  Runs at the
# START of Page.load - before load_document executes the new page's
# scripts - and before the preludes that re-lay per-load surfaces.
REALM_RESET_PRELUDE = (r"""
(function () {
  try {
    var bridge = window.__iv8__;
    if (!bridge) return;
    var KEY = '__realmBase$';
    var PROTOS = (function () {
      var out = [];
      try { if (window.XMLHttpRequest) out.push(XMLHttpRequest.prototype); } catch (e) {}
      return out;
    })();
    var snap = null;
    try { snap = bridge[KEY]; } catch (e) { return; }
    var capture = function (obj) {
      var out = {};
      Object.getOwnPropertyNames(obj).forEach(function (k) {
        var d = Object.getOwnPropertyDescriptor(obj, k);
        if (d) out[k] = d;
      });
      return out;
    };
    var restore = function (obj, base) {
      var changed = 0;
      Object.getOwnPropertyNames(obj).forEach(function (k) {
        var d = Object.getOwnPropertyDescriptor(obj, k);
        var b = base[k];
        if (b === undefined) {
          if (d && d.configurable) { delete obj[k]; changed++; }
          return;
        }
        var same = false;
        if (d && b) {
          if ('value' in d && 'value' in b) {
            same = d.value === b.value && d.writable === b.writable &&
                   d.enumerable === b.enumerable &&
                   d.configurable === b.configurable;
          } else if ('get' in d && 'get' in b) {
            same = d.get === b.get && d.set === b.set &&
                   d.enumerable === b.enumerable &&
                   d.configurable === b.configurable;
          }
        }
        if (!same) {
          try { Object.defineProperty(obj, k, b); changed++; } catch (e) {}
        }
      });
      Object.getOwnPropertyNames(base).forEach(function (k) {
        if (!Object.prototype.hasOwnProperty.call(obj, k)) {
          try { Object.defineProperty(obj, k, base[k]); changed++; }
          catch (e) {}
        }
      });
      return changed;
    };
    if (!snap) {
      snap = { g: capture(window), p: PROTOS.map(capture) };
      Object.defineProperty(bridge, KEY, {
        value: snap, enumerable: false, writable: false,
        configurable: false});
      return;
    }
    restore(window, snap.g);
    PROTOS.forEach(function (p, i) {
      if (snap.p[i]) restore(p, snap.p[i]);
    });
  } catch (e) {}
})();
"""
    + FORM_ELEMENTS_INSTALL
    + INPUT_IDL_DEFAULTS_INSTALL
    + FORM_IDL_FACE_INSTALL)

# The Navigator prototype relocation, also spliced into the document-location
# binder: the SDK's descriptor check runs synchronously while page scripts
# load, before either prelude gets a chance.
NAVIGATOR_SURFACE_JS = "(function(){try{var NP=window.Navigator&&Navigator.prototype;if(NP&&NP!==Object.prototype){var CAP={};['javaEnabled','getBattery','constructor'].forEach(function(k){var dsc=Object.getOwnPropertyDescriptor(NP,k);if(dsc)CAP[k]=dsc.value!==undefined?dsc.value:(dsc.get||null);});var INST=window.navigator;var VAL={};['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','getGamepads','javaEnabled','sendBeacon','vibrate','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){try{VAL[k]=INST[k];}catch(e){VAL[k]=undefined;}});['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','getGamepads','javaEnabled','sendBeacon','vibrate','constructor','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){try{delete NP[k];}catch(e){}});var mk=function(name,val){var f=function(){return val;};try{Object.defineProperty(f,\"name\",{value:\"get \"+name,configurable:true});}catch(e){}try{var nw=(globalThis.__lv8__&&globalThis.__lv8__.wrapNative)||(globalThis.__iv8__&&globalThis.__iv8__.__lv8_internal__&&globalThis.__iv8__.__lv8_internal__.wrapNative);if(nw)nw(f,\"get \"+name);}catch(e){}return f;};['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','getGamepads','javaEnabled','sendBeacon','vibrate','constructor','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].forEach(function(k){if(k===\"constructor\"){try{Object.defineProperty(NP,\"constructor\",{value:CAP.constructor||NP.constructor,writable:true,configurable:true,enumerable:false});}catch(e){}return;}if(['getGamepads','javaEnabled','sendBeacon','vibrate','constructor','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler'].indexOf(k)!==-1){var fn=(k===\"javaEnabled\"||k===\"getBattery\")&&CAP[k]?CAP[k]:VAL[k];if(typeof fn!==\"function\"){fn=function(){return undefined;};try{if(globalThis.__lv8__)globalThis.__lv8__.wrapNative(fn,k);}catch(e){}}try{Object.defineProperty(NP,k,{value:fn,writable:true,configurable:true,enumerable:true});}catch(e){}return;}var g=mk(k,VAL[k]);try{Object.defineProperty(NP,k,{get:g,set:undefined,configurable:true,enumerable:true});}catch(e){}});['vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation','doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled','webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay','hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform','product','userAgent','language','languages','onLine','webdriver','getGamepads','javaEnabled','vibrate','canShare','share','clearAppBadge','setAppBadge','getBattery','getUserMedia','webkitGetUserMedia','requestMIDIAccess','requestMediaKeySystemAccess','registerProtocolHandler','unregisterProtocolHandler','bluetooth','storageBuckets','clipboard','credentials','keyboard','managed','mediaDevices','storage','serviceWorker','virtualKeyboard','wakeLock','deviceMemory','userAgentData','cookieDeprecationLabel','login','ink','mediaCapabilities','hid','locks','gpu','mediaSession','permissions','presentation','usb','xr','serial','deprecatedRunAdAuctionEnforcesKAnonymity'].forEach(function(k){if(k===\"sendBeacon\")return;try{delete INST[k];}catch(e){}});try{if(typeof VAL.sendBeacon===\"function\")Object.defineProperty(INST,\"sendBeacon\",{value:VAL.sendBeacon,writable:true,configurable:true,enumerable:true});}catch(e){}}}catch(e){}})();"



# G4.277 (iteration 2, roadmap doc 2.3 H/I): the "reference-exact" profile hides the
# three deliberate Chrome-style supersets so the observable prototype faces
# match the reference wheel member-for-member.  Keep-lists below are the
# reference-measured own-name sets (work/reference-traces/_ref_own_three.json):
#   Navigator 32, Screen 10, CSSStyleDeclaration 10.
EXACT_MODE_JS = """
(function () {
  function hide(proto, keep) {
    if (!proto) return;
    var names = [];
    try { names = Object.getOwnPropertyNames(proto); } catch (e) { return; }
    for (var i = 0; i < names.length; i++) {
      var k = names[i];
      if (keep.indexOf(k) >= 0) continue;
      try { delete proto[k]; } catch (e) {}
    }
  }
  try {
    var NAV_KEEP = %s;
    var SCR_KEEP = %s;
    var CSS_KEEP = %s;
    if (typeof Navigator === 'function') hide(Navigator.prototype, NAV_KEEP);
    if (typeof Screen === 'function') hide(Screen.prototype, SCR_KEEP);
    if (typeof CSSStyleDeclaration === 'function') {
      hide(CSSStyleDeclaration.prototype, CSS_KEEP);
    }
  } catch (e) {}
})();
""" % (
    '[%s]' % ','.join('"%s"' % k for k in (
        'vendorSub','productSub','vendor','maxTouchPoints','scheduling','userActivation',
        'doNotTrack','geolocation','connection','plugins','mimeTypes','pdfViewerEnabled',
        'webkitTemporaryStorage','webkitPersistentStorage','windowControlsOverlay',
        'hardwareConcurrency','cookieEnabled','appCodeName','appName','appVersion','platform',
        'product','userAgent','language','languages','onLine','webdriver','getGamepads',
        'javaEnabled','sendBeacon','vibrate','constructor')),
    '[%s]' % ','.join('"%s"' % k for k in (
        'availWidth','availHeight','width','height','colorDepth','pixelDepth','availLeft',
        'availTop','orientation','constructor')),
    '[%s]' % ','.join('"%s"' % k for k in (
        'cssText','length','parentRule','cssFloat','getPropertyPriority','getPropertyValue',
        'item','removeProperty','setProperty','constructor')),
)


# G4.278 (iteration 2, roadmap doc 2.3 I): the reference event family shares one
# Event.prototype: every *Event class prototype chains to it and inherits the
# generic members (type/target/bubbles/.../preventDefault/stopPropagation).
# lv8's event classes were flat (each prototype's parent was Object.prototype
# or an internal {constructor}), so `Object.getPrototypeOf(new ErrorEvent())
# -> parent` stopped early and the SDK's face scan saw a different shape.
EVENT_FAMILY_PROTO_JS = """
(function () {
  try {
    var EP = (typeof Event === 'function') ? Event.prototype : null;
    if (!EP) return;
    var names;
    try { names = Object.getOwnPropertyNames(window); } catch (e) { return; }
    for (var i = 0; i < names.length; i++) {
      var n = names[i];
      if (!/Event$/.test(n)) continue;
      var F = null;
      try { F = window[n]; } catch (e) { continue; }
      if (typeof F !== 'function' || !F.prototype || F === Event) continue;
      var p = F.prototype;
      if (!p || p === EP || Object.getPrototypeOf(p) === EP) continue;
      try { Object.setPrototypeOf(p, EP); } catch (e) {}
    }
  } catch (e) {}
})();
"""


# G4.283 (roadmap doc 5.3 P1): descriptor-shape fixes measured against the
# reference wheel for the P1 surface (graphics / audio / network / worker)
# plus the event family.  The table itself is generated - see
# tools/gen_p1_face_fixes.py - and only carries *measured* differences:
#   len    -> function arity (`fn:N:...`)
#   enum   -> enumerability (`...:NE` in lv8 but not in the reference)
#   setter -> reference exposes a read-write accessor where lv8 has a getter
from .p1_face_fixes import P1_FACE_FIXES as _P1_FACE_FIXES

P1_FACE_FIX_JS = """
(function () {
  var FIXES = %s;
  for (var i = 0; i < FIXES.length; i++) {
    var entry = FIXES[i], name = entry[0], member = entry[1],
        kind = entry[2], param = entry[3];
    var F = null;
    try { F = window[name]; } catch (e) { continue; }
    if (typeof F !== 'function' || !F.prototype) continue;
    var isCtor = (member === 'constructor');
    var target = isCtor ? F : F.prototype;
    var d = null;
    try {
      d = isCtor ? Object.getOwnPropertyDescriptor(F, 'length')
                 : Object.getOwnPropertyDescriptor(target, member);
    } catch (e) { continue; }
    if (!d || d.configurable === false) continue;
    try {
      if (kind === 'len' && isCtor) {
        Object.defineProperty(F, 'length',
                              { value: param, configurable: true });
      } else if (kind === 'len' && typeof d.value === 'function') {
        Object.defineProperty(d.value, 'length',
                              { value: param, configurable: true });
      } else if (kind === 'unset' && d.get) {
        Object.defineProperty(target, member, {
          get: d.get, enumerable: d.enumerable, configurable: true });
      } else if (kind === 'enum') {
        var desc = { enumerable: true, configurable: true };
        if (d.get || d.set) { desc.get = d.get; desc.set = d.set; }
        else { desc.value = d.value; desc.writable = d.writable; }
        Object.defineProperty(target, member, desc);
      } else if (kind === 'setter' && d.get) {
        Object.defineProperty(target, member, {
          get: d.get, set: function () {}, enumerable: d.enumerable,
          configurable: true });
      }
    } catch (e) {}
  }
})();
""" % __import__('json').dumps(_P1_FACE_FIXES)


# G4.296 (iteration 3, M1): reference-exact face install for a *family*.
# The table is generated by tools/gen_ref_element_faces.py from the measured
# reference rows (members, kinds, arities, enumerability, default values).  Rules:
#   * never remove or overwrite an existing own member of an instance;
#   * missing members are added with the reference kind/arity/enumerability
#     and the reference default value read from a real reference instance;
#   * shape mismatches are corrected in place (arity / setter / enumerability).
from .ref_face_html import REF_FACE_HTML as _REF_FACE_HTML
from .ref_face_dom import REF_FACE_DOM as _REF_FACE_DOM
from .ref_face_svg import REF_FACE_SVG as _REF_FACE_SVG
from .ref_face_gpu import REF_FACE_GPU as _REF_FACE_GPU
from .ref_face_event import REF_FACE_EVENT as _REF_FACE_EVENT
from .ref_face_xpath import REF_FACE_XPATH as _REF_FACE_XPATH
from .ref_face_misc import REF_FACE_MISC as _REF_FACE_MISC
from .ref_face_fix2 import REF_FACE_FIX2 as _REF_FACE_FIX2
from .ref_face_fix3 import REF_FACE_FIX3 as _REF_FACE_FIX3
from .ref_face_tags import REF_FACE_TAGS as _REF_FACE_TAGS
from .ref_face_symbols import REF_FACE_SYMBOLS as _REF_FACE_SYMBOLS
from .ref_face_thinvalues import REF_FACE_THINVALUES as _REF_FACE_THINVALUES

# every generated family table participates in one install pass
_REF_FAMILY_TABLES = [_REF_FACE_HTML, _REF_FACE_DOM, _REF_FACE_SVG,
                      _REF_FACE_GPU, _REF_FACE_EVENT, _REF_FACE_XPATH,
                      _REF_FACE_MISC, _REF_FACE_FIX2, _REF_FACE_FIX3,
                      _REF_FACE_TAGS, _REF_FACE_SYMBOLS,
                      _REF_FACE_THINVALUES]

REF_FAMILY_FACE_JS = """
(function () {
  var TABLES = %s;
  function ownValue(obj, k) {
    var d = null;
    try { d = Object.getOwnPropertyDescriptor(obj, k); } catch (e) { return undefined; }
    if (!d || d.get || d.set) return undefined;
    return d.value;
  }
  function defineAccessor(P, k, rw, value, enumerable) {
    var cur = null;
    try { cur = Object.getOwnPropertyDescriptor(P, k); } catch (e) {}
    if (cur && cur.configurable === false) return;
    // '@undef' sentinel: the reference accessor answers undefined
    // (Touch.target, M11) - distinct from an explicit null constant
    var undefSentinel = value === '@undef';
    // '@window' sentinel: the reference accessor hands back the LIVE window
    // (Document.defaultView, M19) - distinct from a string constant
    var windowSentinel = value === '@window';
    var get = function () {
      if (undefSentinel) return undefined;
      if (windowSentinel) return window;
      var own = ownValue(this, k);
      return (own === undefined) ? value : own;
    };
    var desc = { get: get, configurable: true, enumerable: !!enumerable };
    if (rw) {
      desc.set = function (v) {
        try {
          Object.defineProperty(this, k,
            { value: v, writable: true, configurable: true, enumerable: true });
        } catch (e) {}
      };
    }
    try { Object.defineProperty(P, k, desc); } catch (e) {}
  }
  function defineMethod(P, k, arity, enumerable) {
    var cur = null;
    try { cur = Object.getOwnPropertyDescriptor(P, k); } catch (e) {}
    if (cur) return;
    var fn = function () { return undefined; };
    try { Object.defineProperty(fn, 'name', { value: k, configurable: true }); } catch (e) {}
    try { Object.defineProperty(fn, 'length', { value: arity || 0, configurable: true }); } catch (e) {}
    try {
      Object.defineProperty(P, k,
        { value: fn, writable: true, configurable: true, enumerable: !!enumerable });
    } catch (e) {}
  }
  for (var ti = 0; ti < TABLES.length; ti++) {
  var T = TABLES[ti];
  for (var i = 0; i < T.length; i++) {
    var row = T[i], name = row[0], member = row[1], kind = row[2],
        param = row[3], value = row[4], enumerable = row[5];
    var F = null;
    try { F = window[name]; } catch (e) { continue; }
    if (typeof F !== 'function' || !F.prototype) continue;
    var P = F.prototype;
    // G4.306: tag / ctor-name are *missing* members, handle them before the
    // descriptor-based fix path (there is no existing descriptor to patch).
    if (kind === 'tag') {
      try {
        Object.defineProperty(P, Symbol.toStringTag,
          { value: value, configurable: true });
      } catch (e) {}
      continue;
    }
    if (kind === 'ctor-name') {
      try { Object.defineProperty(F, 'name',
            { value: value, configurable: true }); } catch (e) {}
      continue;
    }
    // G4.312: symbol-face kinds (measured against the reference wheel;
    // see ref_face_symbols.py).  wrapNative gives the JS stubs the native
    // function source the reference shows.
    var symOf = { '@iterator': null, '@unscopables': null };
    try { symOf['@iterator'] = Symbol.iterator; } catch (e0) {}
    try { symOf['@unscopables'] = Symbol.unscopables; } catch (e0) {}
    function lv8NativeStub(name, length) {
      var stub = function () { return undefined; };
      try { Object.defineProperty(stub, 'name',
            { value: name, configurable: true }); } catch (e1) {}
      try { Object.defineProperty(stub, 'length',
            { value: length || 0, configurable: true }); } catch (e2) {}
      try { var nw = (globalThis.__lv8__ && globalThis.__lv8__.wrapNative) ||
            (globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__ &&
             globalThis.__iv8__.__lv8_internal__.wrapNative);
            if (nw) nw(stub, name); } catch (e3) {}
      return stub;
    }
    if (kind === 'sym-it') {
      var SYM = symOf['@iterator'];
      if (SYM) {
        var it = null;
        if (param === 'arr') {
          try { it = Array.prototype[SYM]; } catch (e4) {}
        } else if (param === 'entries') {
          it = lv8NativeStub('entries', 0);
        } else if (param === 'anonE') {
          it = lv8NativeStub('', 0);
        } else { it = lv8NativeStub('', 0); }
        if (it) {
          try { Object.defineProperty(P, SYM,
                { value: it, writable: true, configurable: true,
                  enumerable: !!enumerable }); } catch (e5) {}
        }
      }
      continue;
    }
    if (kind === 'sym-usc') {
      var US = symOf['@unscopables'];
      if (US) {
        var keysU = String(param).split('|');
        var objU = {};
        for (var ui = 0; ui < keysU.length; ui++) objU[keysU[ui]] = true;
        try { Object.defineProperty(P, US,
              { value: objU, writable: true, configurable: true,
                enumerable: !!enumerable }); } catch (e6) {}
      }
      continue;
    }
    if (kind === 'sym-order') {
      // re-insert own well-known symbols in the reference order
      // (toStringTag, iterator, unscopables), preserving descriptors
      var orderS = [];
      try { orderS.push([Symbol.toStringTag,
            Object.getOwnPropertyDescriptor(P, Symbol.toStringTag)]); } catch (e11) {}
      try { orderS.push([Symbol.iterator,
            Object.getOwnPropertyDescriptor(P, Symbol.iterator)]); } catch (e12) {}
      try { orderS.push([Symbol.unscopables,
            Object.getOwnPropertyDescriptor(P, Symbol.unscopables)]); } catch (e13) {}
      var touched = false;
      for (var oi = 0; oi < orderS.length; oi++) {
        if (orderS[oi][1]) {
          try { delete P[orderS[oi][0]]; touched = true; } catch (e14) {}
        }
      }
      if (touched) {
        for (var oj = 0; oj < orderS.length; oj++) {
          var od = orderS[oj][1];
          if (od) { try { Object.defineProperty(P, orderS[oj][0], od); } catch (e15) {} }
        }
      }
      continue;
    }
    if (kind === 'sym-del') {
      var SD = symOf['@iterator'];
      if (SD) { try { delete P[SD]; } catch (e7) {} }
      continue;
    }
    if (kind === 'tagne') {
      // re-define the existing tag non-enumerable (delete first: the
      // template-installed descriptor would silently ignore e-change)
      var TD = null;
      try { TD = Object.getOwnPropertyDescriptor(P, Symbol.toStringTag);
      } catch (e8) {}
      if (TD && TD.configurable) {
        var val8 = TD.value;
        try { delete P[Symbol.toStringTag]; } catch (e9) {}
        try { Object.defineProperty(P, Symbol.toStringTag,
              { value: val8, writable: false, enumerable: false,
                configurable: true }); } catch (e10) {}
      }
      continue;
    }
    if (kind === 'fn') { defineMethod(P, member, param, enumerable); continue; }
    if (kind === 'rt' || kind === 'rw') {
      defineAccessor(P, member, kind === 'rw', value, enumerable);
      continue;
    }
    if (kind === 'val') {
      var d0 = null;
      try { d0 = Object.getOwnPropertyDescriptor(P, member); } catch (e) {}
      if (!d0) {
        try {
          Object.defineProperty(P, member,
            { value: value, writable: true, configurable: true,
              enumerable: !!enumerable });
        } catch (e) {}
      }
      continue;
    }
    // in-place shape fixes (same semantics as the P1 fix table).
    // G4.303: template-shaped prototypes silently ignore an
    // accessor/data redefinition, so delete first (the only reliable way
    // measured on this engine) and re-define immediately after.
    var d = null;
    try { d = Object.getOwnPropertyDescriptor(P, member); } catch (e) { continue; }
    if (!d || d.configurable === false) continue;
    try {
      if (kind === 'len' && typeof d.value === 'function') {
        Object.defineProperty(d.value, 'length',
                              { value: param, configurable: true });
      } else if (kind === 'setter' && d.get) {
        try { delete P[member]; } catch (e) {}
        Object.defineProperty(P, member,
          { get: d.get, set: function () {}, enumerable: d.enumerable,
            configurable: true });
      } else if (kind === 'unset' && d.get) {
        try { delete P[member]; } catch (e) {}
        try {
          Object.defineProperty(P, member,
            { get: d.get, enumerable: !!enumerable, configurable: true });
        } catch (e) {}
      } else if (kind === 'name' && typeof d.value === 'function') {
        // the generator stores the wanted name in the value column (param is
        // unused for this kind) - see tools/gen_ref_element_faces.py
        var want = (value === null || value === undefined) ? param : value;
        try { Object.defineProperty(d.value, 'name',
              { value: want, configurable: true }); } catch (e) {}
      } else if (kind === 'acc') {
        var av = d.value;
        try { delete P[member]; } catch (e) {}
        var ad = { get: function () { return av; }, configurable: true,
                   enumerable: !!enumerable };
        if (param) { ad.set = function (v) { av = v; }; }
        try { Object.defineProperty(P, member, ad); } catch (e) {}
      } else if (kind === 'data') {
        var dv = d.value;
        if (d.get) { try { dv = d.get.call(P); } catch (e) { dv = undefined; } }
        try { delete P[member]; } catch (e) {}
        try {
          Object.defineProperty(P, member,
            { value: dv, writable: true, configurable: true,
              enumerable: !!enumerable });
        } catch (e) {}
      } else if (kind === 'enum') {
        var dd = { enumerable: !!enumerable, configurable: true };
        if (d.get || d.set) { dd.get = d.get; dd.set = d.set; }
        else { dd.value = d.value; dd.writable = d.writable; }
        try { delete P[member]; } catch (e) {}
        var after = null;
        try { after = Object.getOwnPropertyDescriptor(P, member); } catch (e) {}
        if (after) {  // delete was refused: keep the original in place
          Object.defineProperty(P, member, dd);
        } else {
          Object.defineProperty(P, member, dd);
        }
      }
    } catch (e) {}
  }
  }
  // G4.379: media-level own-key order.  for-in on media elements walks the
  // concrete prototype first, then HTMLMediaElement.prototype whose key
  // order is observable - the reference's 46-member order starts at
  // `error` while the walk 4-pack (play/pause/canPlayType/paused) used to
  // split the head keys.  Re-insertion preserves each descriptor exactly.
  (function () {
    var MEDIA_ORDER = ('error,src,currentSrc,crossOrigin,networkState,preload,' +
      'buffered,readyState,seeking,currentTime,duration,paused,' +
      'defaultPlaybackRate,playbackRate,played,seekable,ended,autoplay,loop,' +
      'load,canPlayType,play,pause,controls,volume,muted,defaultMuted,' +
      'audioTracks,videoTracks,textTracks,addTextTrack,controlsList,' +
      'preservesPitch,latencyHint,webkitAudioDecodedByteCount,' +
      'webkitVideoDecodedByteCount,NETWORK_EMPTY,NETWORK_IDLE,' +
      'NETWORK_LOADING,NETWORK_NO_SOURCE,HAVE_NOTHING,HAVE_METADATA,' +
      'HAVE_CURRENT_DATA,HAVE_FUTURE_DATA,HAVE_ENOUGH_DATA,constructor'
      ).split(',');
    var VIDEO_ORDER = ('width,height,videoWidth,videoHeight,poster,' +
      'playsInline,webkitDecodedFrameCount,webkitDroppedFrameCount,' +
      'constructor').split(',');
    function reorder(P, order) {
      if (!P) return;
      var descs = {}, all = [], i, k, d;
      for (i = 0; i < order.length; i++) {
        k = order[i];
        try { d = Object.getOwnPropertyDescriptor(P, k); } catch (e0) { d = null; }
        if (d) { descs[k] = d; all.push(k); }
      }
      var extra = [];
      try { extra = Object.getOwnPropertyNames(P).filter(function (n) {
        return order.indexOf(n) < 0; }); } catch (e1) {}
      for (i = 0; i < extra.length; i++) {
        k = extra[i];
        try { d = Object.getOwnPropertyDescriptor(P, k); } catch (e2) { d = null; }
        if (d) { descs[k] = d; all.push(k); }
      }
      for (i = 0; i < all.length; i++) { try { delete P[all[i]]; } catch (e3) {} }
      for (i = 0; i < all.length; i++) {
        k = all[i]; d = descs[k]; if (!d) continue;
        try {
          if (Object.prototype.hasOwnProperty.call(P, k)) continue;
          Object.defineProperty(P, k, d);
        } catch (e4) {}
      }
    }
    try { reorder(window.HTMLMediaElement && HTMLMediaElement.prototype,
                  MEDIA_ORDER); } catch (e5) {}
    try { reorder(window.HTMLVideoElement && HTMLVideoElement.prototype,
                  VIDEO_ORDER); } catch (e6) {}
    try { reorder(window.HTMLAudioElement && HTMLAudioElement.prototype,
                  ['constructor']); } catch (e7) {}
  })();
  // G4.383: reference getters are named "get <member>" (Function#toString
  // renders "function get valueAsString() { [native code] }"); accessors
  // planted through object-literal inference kept just "get", and the
  // native-shape pass froze that into every generic accessor the ledger's
  // value-diff rows flagged.  Sweep every window-reachable interface
  // prototype and rename whatever still carries the inferred name.
  (function () {
    function sweep(P) {
      if (!P) return 0;
      var n = 0;
      var names;
      try { names = Object.getOwnPropertyNames(P); } catch (e0) { return 0; }
      for (var i = 0; i < names.length; i++) {
        var k = names[i], d = null;
        try { d = Object.getOwnPropertyDescriptor(P, k); } catch (e1) {}
        if (!d) continue;
        try {
          if (d.get && (d.get.name === 'get' || d.get.name === ''))
            Object.defineProperty(d.get, 'name',
              { value: 'get ' + k, configurable: true });
        } catch (e2) {}
        try {
          if (d.set && (d.set.name === 'set' || d.set.name === ''))
            Object.defineProperty(d.set, 'name',
              { value: 'set ' + k, configurable: true });
        } catch (e3) {}
      }
      return 1;
    }
    var ctors;
    try { ctors = Object.getOwnPropertyNames(window); } catch (e4) { return; }
    for (var i = 0; i < ctors.length; i++) {
      var k = ctors[i];
      var F = null;
      try { F = window[k]; } catch (e5) {}
      // lowercase-prefixed webkit* interfaces ride the same prototype face
      if (typeof F !== 'function' || !F.prototype) continue;
      var p = F.prototype, depth = 0;
      while (p && depth < 4) {
        sweep(p);
        try { p = Object.getPrototypeOf(p); } catch (e6) { break; }
        depth++;
      }
    }
    // G4.383b: measured arity on the Navigator surface (the reference
    // wheel's methods carry real arities; lv8's stubs were arity 0).
    try {
      var NAV_ARITY = { registerProtocolHandler: 2,
        requestMediaKeySystemAccess: 2, unregisterProtocolHandler: 2,
        webkitGetUserMedia: 3, getUserMedia: 3 };
      var NP2 = window.Navigator && Navigator.prototype;
      if (NP2) Object.keys(NAV_ARITY).forEach(function (k2) {
        var d2 = Object.getOwnPropertyDescriptor(NP2, k2);
        if (d2 && d2.value && typeof d2.value === 'function' &&
            d2.value.length !== NAV_ARITY[k2])
          Object.defineProperty(d2.value, 'length',
            { value: NAV_ARITY[k2], configurable: true });
      });
    } catch (e8) {}
    // G4.383b: the reference exposes Crypto.prototype.subtle as an
    // accessor; convert the planted data object in place (identity kept).
    try {
      var CP = window.Crypto && Crypto.prototype;
      var sd = CP && Object.getOwnPropertyDescriptor(CP, 'subtle');
      if (sd && !sd.get) {
        var inst = sd.value;
        var getter = function () { return inst; };
        try { Object.defineProperty(getter, 'name',
              { value: 'get subtle', configurable: true }); } catch (e9) {}
        Object.defineProperty(CP, 'subtle',
          { get: getter, enumerable: true,
            configurable: true });
      }
    } catch (e10) {}
  })();
})();
""" % __import__('json').dumps(_REF_FAMILY_TABLES)


# G4.364: XPath early fix.  The document parse freezes XPathResult.prototype
# partway through the load (constants all 0, weak type guards) and later
# passes only fill what is still missing - so the guarded accessors and the
# correct constant values must be planted at parse time, before that pass
# runs.  Every guard message is byte-measured from the reference
# (work/reference-traces/_g364_xpath_msgs.json); the type of the result products
# rides the shared WeakMap the late xpath-family pass fills.
XPATH_FAMILY_EARLY_JS = """
(function () {
  'use strict';
  var XR = globalThis.XPathResult;
  if (typeof XR !== 'function' || !XR.prototype) return;
  function redef(o, k, d) {
    try { Object.defineProperty(o, k, d); } catch (e) {}
  }
  // constants only (G4.366): planting the guarded accessors/methods here
  // too introduced a +1 swept member in the challenge battery (the whole
  // early accessor set is what diverges - see PROGRESS G4.366); the
  // relayout only fills what is still missing, so correct values planted
  // at parse time survive it.
  var CONSTS = [['ANY_TYPE', 0], ['NUMBER_TYPE', 1], ['STRING_TYPE', 2],
    ['BOOLEAN_TYPE', 3], ['UNORDERED_NODE_ITERATOR_TYPE', 4],
    ['ORDERED_NODE_ITERATOR_TYPE', 5], ['UNORDERED_NODE_SNAPSHOT_TYPE', 6],
    ['ORDERED_NODE_SNAPSHOT_TYPE', 7], ['ANY_UNORDERED_NODE_TYPE', 8],
    ['FIRST_ORDERED_NODE_TYPE', 9]];
  CONSTS.forEach(function (pair) {
    if (Object.prototype.hasOwnProperty.call(XR.prototype, pair[0])) return;
    redef(XR.prototype, pair[0], { value: pair[1], writable: false,
      enumerable: true, configurable: true });
  });
})();
"""


# G4.364: XPath family alignment.  The three XPath classes carried thinwrap
# members whose behavior diverges from the reference (all ten XPathResult
# constants answered 0, the evaluator methods threw on missing internal
# slots, createNSResolver answered undefined, and the prototypes hung off
# HTMLElement.prototype so for-in swept 276 element-chain members), and the
# document chains carried return-null stubs - so the challenge battery's
# createExpression branch popped null and skipped its reads (D1 fork).
# Reference ground truth (work/reference-traces/_g364_xpath_*.json): products own
# no members, result defaults to STRING semantics, iterators/snapshots are
# empty, and every guard message is byte-measured.  Callables get
# native-styled sources through the wrap registry.
XPATH_FAMILY_FIX_JS = """
(function () {
  'use strict';
  var WN = null;
  try { WN = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__ &&
        globalThis.__iv8__.__lv8_internal__.wrapNative; } catch (e0) {}
  function nat(fn, name, length) {
    try { Object.defineProperty(fn, 'name',
          { value: name, configurable: true }); } catch (e1) {}
    try { Object.defineProperty(fn, 'length',
          { value: length, configurable: true }); } catch (e2) {}
    try { if (WN) WN(fn, name); } catch (e3) {}
    return fn;
  }
  function defn(o, k, d) {
    // reorder-safe: drop-then-redefine moves the key to the end - used
    // only where the reference order has the member last (constructor).
    try { delete o[k]; } catch (e4) {}
    try { Object.defineProperty(o, k, d); } catch (e5) {}
  }
  function redef(o, k, d) {
    // in-place: keeps the current key position (for-in order must not move)
    try { Object.defineProperty(o, k, d); } catch (e6) {}
  }
  function req(name, target, args, n) {
    if (args.length < n) {
      throw new TypeError("Failed to execute '" + name + "' on '" + target +
        "': " + n + " argument" + (n > 1 ? 's' : '') + " required, but only " +
        args.length + ' present.');
    }
  }
  var XP = globalThis.XPathExpression, XR = globalThis.XPathResult,
      XE = globalThis.XPathEvaluator;
  if (typeof XP !== 'function' || typeof XR !== 'function' ||
      typeof XE !== 'function' || !XP.prototype || !XR.prototype ||
      !XE.prototype) return;
  var wm = typeof WeakMap === 'function' ? new WeakMap() : null;

  // -- result products: own-free, STRING semantics by default (reference: ANY_TYPE
  //    resolves to resultType 2; iterator/snapshot result sets are empty)
  function normType(type) {
    var t = (typeof type === 'number') ? (type | 0) : 2;
    return t === 0 ? 2 : t;
  }
  function mkResult(type) {
    var r = Object.create(XR.prototype);
    if (wm) wm.set(r, { t: normType(type) });
    return r;
  }
  function mkExpression(expr) {
    var e = Object.create(XP.prototype);
    if (wm) wm.set(e, { expr: expr });
    return e;
  }
  // Products carry the guarded result accessors as own properties: the
  // payload's mid-page relayout reinstalls the native (weak-guard)
  // prototype accessors during drain, so prototype-level guards do not
  // survive to the reads.  The messages are the byte-measured reference
  // texts; the type rides the shared state map the early pass created.
  function mkResult(expr, contextNode, resolver, type, result) {
    var r = Object.create(XR.prototype);
    var t = normType(type);
    if (wm) wm.set(r, { t: t });
    function ownGuard(k, kind, ok, value) {
      Object.defineProperty(r, k, { get: function () {
        if (!ok(t)) {
          throw new TypeError("Failed to read the '" + k +
            "' property from 'XPathResult': The result type is not a " +
            kind + '.');
        }
        return value;
      }, configurable: true, enumerable: true });
    }
    Object.defineProperty(r, 'resultType', {
      get: function () { return t; }, configurable: true, enumerable: true });
    ownGuard('numberValue', 'number', function (x) { return x === 1; }, 0);
    ownGuard('stringValue', 'string', function (x) { return x === 2; }, '');
    ownGuard('booleanValue', 'boolean', function (x) { return x === 3; },
      false);
    ownGuard('singleNodeValue', 'single node',
      function (x) { return x === 8 || x === 9; }, null);
    Object.defineProperty(r, 'invalidIteratorState', {
      get: function () { return t === 4 || t === 5 ? false : false; },
      configurable: true, enumerable: true });
    ownGuard('snapshotLength', 'snapshot',
      function (x) { return x === 6 || x === 7; }, 0);
    Object.defineProperty(r, 'iterateNext', {
      value: function iterateNext() {
        if (!(t === 4 || t === 5)) {
          throw new TypeError(
            "Failed to execute 'iterateNext' on 'XPathResult': " +
            'The result type is not an iterator.');
        }
        return null;
      }, writable: true, enumerable: true, configurable: true });
    Object.defineProperty(r, 'snapshotItem', {
      value: function snapshotItem(index) {
        if (arguments.length < 1) {
          throw new TypeError(
            "Failed to execute 'snapshotItem' on 'XPathResult': " +
            '1 argument required, but only 0 present');
        }
        if (!(t === 6 || t === 7)) {
          throw new TypeError(
            "Failed to execute 'snapshotItem' on 'XPathResult': " +
            'The result type is not a snapshot.');
        }
        return null;
      }, writable: true, enumerable: true, configurable: true });
    return r;
  }

  // -- XPathExpression.prototype.evaluate(contextNode, type, result):
  //    delegate to the native machinery with the stored expression
  redef(XP.prototype, 'evaluate', {
    value: nat(function evaluate(contextNode, type, result) {
      req('evaluate', 'XPathExpression', arguments, 1);
      return mkResult('.', contextNode, undefined,
        arguments[1], arguments[2]);
    }, 'evaluate', 1), writable: true, enumerable: true, configurable: true });

  // -- XPathEvaluator.prototype: working evaluator methods
  redef(XE.prototype, 'createExpression', {
    value: nat(function createExpression(expression) {
      req('createExpression', 'XPathEvaluator', arguments, 1);
      return mkExpression(arguments[0]);
    }, 'createExpression', 1), writable: true, enumerable: true,
    configurable: true });
  redef(XE.prototype, 'createNSResolver', {
    value: nat(function createNSResolver(nodeResolver) {
      req('createNSResolver', 'XPathEvaluator', arguments, 1);
      return arguments[0];
    }, 'createNSResolver', 1), writable: true, enumerable: true,
    configurable: true });
  redef(XE.prototype, 'evaluate', {
    value: nat(function evaluate(expression, contextNode, resolver, type,
                                 result) {
      req('evaluate', 'XPathEvaluator', arguments, 2);
      return mkResult(arguments[0], arguments[1], arguments[2],
        arguments[3], arguments[4]);
    }, 'evaluate', 2), writable: true, enumerable: true, configurable: true });

  // -- constructors: the reference answers TypeError('Illegal constructor')
  //    for `new XPathExpression` / `new XPathResult` (XPathEvaluator stays
  //    constructible); keep the prototype identity so instanceof holds
  function replaceGlobal(name, g) {
    try {
      var cur = Object.getOwnPropertyDescriptor(globalThis, name);
      Object.defineProperty(globalThis, name, {
        value: g, writable: cur ? cur.writable : true,
        enumerable: cur ? cur.enumerable : false,
        configurable: cur ? cur.configurable : true });
    } catch (e6) {}
  }
  function illegalCtor(name, proto, original) {
    var g = function () { throw new TypeError('Illegal constructor'); };
    nat(g, name, 0);
    try { g.prototype = proto; } catch (e7) {}
    defn(proto, 'constructor', { value: g, writable: true,
      enumerable: false, configurable: true });
    // carry the original's own statics across (the XPathResult constants
    // 0..9 live on the constructor; dropping them regressed the surface)
    if (original) {
      Object.getOwnPropertyNames(original).forEach(function (k) {
        if (k === 'length' || k === 'name' || k === 'prototype' ||
            k === 'arguments' || k === 'caller') return;
        try {
          var d = Object.getOwnPropertyDescriptor(original, k);
          if (d) Object.defineProperty(g, k, d);
        } catch (e21) {}
      });
    }
    return g;
  }
  var xpeCtor = illegalCtor('XPathExpression', XP.prototype, XP);
  var xprCtor = illegalCtor('XPathResult', XR.prototype, XR);
  replaceGlobal('XPathExpression', xpeCtor);
  replaceGlobal('XPathResult', xprCtor);
  // constructor lands last on each prototype (reference own-key order)
  defn(XE.prototype, 'constructor', { value: XE, writable: true,
    enumerable: false, configurable: true });
  try { delete XE.__lv8ThinWrap; } catch (e8) {}
  try { Object.defineProperty(XE, '__lv8ThinWrap',
        { value: undefined, writable: false, enumerable: false,
          configurable: true }); } catch (e9) {}
  try { delete XE.__lv8ThinWrap; } catch (e10) {}

  // -- reference chains: the three prototypes hang off Object.prototype
  try { Object.setPrototypeOf(XP.prototype, Object.prototype); } catch (e11) {}
  try { Object.setPrototypeOf(XR.prototype, Object.prototype); } catch (e12) {}
  try { Object.setPrototypeOf(XE.prototype, Object.prototype); } catch (e13) {}

  // -- document-level factories (target 'Document', byte-measured messages;
  //    evaluate delegates to the native machinery for real products)
  function installDocXpath(proto) {
    if (!proto) return;
    redef(proto, 'createExpression', {
      value: nat(function createExpression(expression) {
        req('createExpression', 'Document', arguments, 1);
        return mkExpression(arguments[0]);
      }, 'createExpression', 1), writable: true, enumerable: true,
      configurable: true });
    redef(proto, 'evaluate', {
      value: nat(function evaluate(expression, contextNode, resolver, type,
                                   result) {
        req('evaluate', 'Document', arguments, 2);
        return mkResult(arguments[0], arguments[1], arguments[2],
          arguments[3], arguments[4]);
      }, 'evaluate', 2), writable: true, enumerable: true,
      configurable: true });
    redef(proto, 'createNSResolver', {
      value: nat(function createNSResolver(nodeResolver) {
        req('createNSResolver', 'Document', arguments, 1);
        return arguments[0];
      }, 'createNSResolver', 1), writable: true, enumerable: true,
      configurable: true });
  }
  // the document face level: the chain is re-leveled by several passes, so
  // target the level that carries the face (own createElement / a wide own
  // set) rather than a constructor name
  function docFaceLevel(doc) {
    var q = Object.getPrototypeOf(doc), i = 0;
    while (q && i < 6) {
      if (Object.prototype.hasOwnProperty.call(q, 'createElement') ||
          Object.getOwnPropertyNames(q).length >= 30) return q;
      q = Object.getPrototypeOf(q); i++;
    }
    return Object.getPrototypeOf(doc);
  }
  // G4.368: parsed/iframe documents get fresh per-call prototypes - the
  // native MakeDocumentObject calls this installer (G4.391: string key on
  // the internal bridge - a Symbol.for globalThis key is getOwnPropertySymbols
  // visible and reference's window has zero own symbols) with
  // each new document, giving the fresh face level the same three members
  // the page document carries.
  try {
    var __xpBridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
    if (__xpBridge) __xpBridge.lv8XPathInstallDoc = function (doc) {
      try {
        // reference's DOMParser/DOMImplementation products resolve directly from
        // the realm-wide Document.prototype (not from a private native
        // template), sharing identity with the page document's members.
        if (doc && globalThis.Document && Document.prototype)
          Object.setPrototypeOf(doc, Document.prototype);
        installDocXpath(Document.prototype);
      } catch (e19b) {}
    };
  } catch (e19c) {}
  // main document chain (the battery surface)
  try { installDocXpath(docFaceLevel(document)); } catch (e15) {}
  // parsed-document share (DOMParser / implementation.createHTMLDocument
  // products share one Document-level proto per realm)
  try {
    var pdoc = new DOMParser().parseFromString('<x/>', 'text/html');
    installDocXpath(docFaceLevel(pdoc));
  } catch (e17) {}
  // the interface prototype itself (probes invoke through Document.prototype
  // with .call)
  try {
    if (globalThis.Document && globalThis.Document.prototype)
      installDocXpath(globalThis.Document.prototype);
  } catch (e18) {}
})();
"""


# G4.306: reference-exact native-shape registry + narrow tag fixes that the
# ledger measured (Navigation constructor prints "class ..." in lv8 because
# the binder script creates it; the reference prints a native function).
NATIVE_SHAPE_JS = """
(function () {
  try {
    if (typeof window.__LV8_NATIVE_FN_NAMES__ !== 'object' || !window.__LV8_NATIVE_FN_NAMES__) {
      window.__LV8_NATIVE_FN_NAMES__ = {};
    }
    window.__LV8_NATIVE_FN_NAMES__['Navigation'] = 1;
  } catch (e) {}
  try {
    var N = window.Navigation;
    // G4.393: the chain pass links Navigation.prototype onto
    // EventTarget.prototype, so the inherited tag answers 'EventTarget' -
    // force the own tag whenever the ANSWER is not the reference's brand
    // (the old undefined-only guard skipped the re-define).
    if (N && N.prototype &&
        String(N.prototype[Symbol.toStringTag] || '') !== 'Navigation') {
      Object.defineProperty(N.prototype, Symbol.toStringTag, {
        value: 'Navigation', configurable: true });
    }
  } catch (e) {}
})();
"""


# G4.308 (M3 line-1): own-symbol face of the page singletons.  The reference
# keeps ZERO own symbols on window/document/navigator/history/screen/storage/
# indexedDB/caches/performance/crypto/cookieStore/location: Symbol.toStringTag
# is a *prototype* property there, and location owns a frozen undefined-valued
# Symbol.toPrimitive instead of a tag.  lv8's C++ object templates install the
# tag on each instance (configurable, so removable here); where the singleton's
# prototype chain is internal and untagged, the brand value is restored by
# tagging the prototype object the chain actually walks (the reference tags
# window.X.prototype at the same depth).  The two engine-internal symbols the
# bootstrap/polyfills used to park on the global (c9 canary, style-sheet
# cache) moved off the face (closure + internal channel); the deletes here
# are a defensive sweep for state installed before this pass.
REF_SYMOWN_JS = """
(function () {
  var T = Symbol.toStringTag;
  function del(o, s) {
    try {
      if (Object.getOwnPropertyDescriptor(o, s) !== undefined) delete o[s];
    } catch (e) {}
  }
  function tagProto(o, brand) {
    try {
      if (String(o[T] || '') === brand) return;
      var p = Object.getPrototypeOf(o);
      while (p) {
        var d = null;
        try { d = Object.getOwnPropertyDescriptor(p, T); } catch (e) {}
        if (d) {
          if (String(p[T] || '') === brand) return;
          break;
        }
        p = Object.getPrototypeOf(p);
      }
      p = Object.getPrototypeOf(o);
      if (p)
        Object.defineProperty(p, T,
          { value: brand, writable: false, enumerable: false,
            configurable: true });
    } catch (e) {}
  }
  function fix(name, brand) {
    var o = null;
    try { o = window[name]; } catch (e) { return; }
    if (!o || (typeof o !== 'object' && typeof o !== 'function')) return;
    del(o, T);
    tagProto(o, brand);
  }
  fix('window', 'Window');
  fix('document', 'HTMLDocument');
  fix('navigator', 'Navigator');
  fix('history', 'History');
  fix('screen', 'Screen');
  fix('localStorage', 'Storage');
  fix('sessionStorage', 'Storage');
  fix('indexedDB', 'IDBFactory');
  fix('caches', 'CacheStorage');
  fix('performance', 'Performance');
  fix('crypto', 'Crypto');
  fix('cookieStore', 'CookieStore');
  // location: no own tag (Location.prototype carries the brand); the
  // reference owns a frozen, undefined-valued Symbol.toPrimitive.
  try {
    var lo = window.location;
    if (lo) {
      del(lo, T);
      tagProto(lo, 'Location');
      if (Object.getOwnPropertyDescriptor(lo, Symbol.toPrimitive) === undefined)
        Object.defineProperty(lo, Symbol.toPrimitive,
          { value: undefined, writable: false, enumerable: false,
            configurable: false });
    }
  } catch (e) {}
  // defensive: any pre-pass internal state parked on the global face
  del(window, Symbol.for('lv8c9dbg'));
  del(window, Symbol.for('lv8SheetCache'));
})();
"""
