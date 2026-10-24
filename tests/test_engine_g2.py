"""G2 DOM tests: the oracle's page-load/DOM subset re-hosted on the C++
engine facade (``lv8.engine``). Expectations mirror the oracle semantics;
engine-native differences (interface naming, serialization) follow Chrome.
"""

from __future__ import annotations

import pytest

from lv8.engine import JSContext, Page
from lv8.errors import NotImplementedYetError

PAGE_HTML = """
    <html><head><title>Demo</title></head><body>
      <div id='app' class='card'>hello <strong>lv8</strong></div>
      <p class='note'>a</p><p class='note'>b</p>
      <script>globalThis.steps = ['inline'];</script>
      <script>steps.push(document.querySelector('#app').textContent.trim());</script>
      <script>document.addEventListener('DOMContentLoaded', () => {
          globalThis.domReady = document.readyState; });</script>
    </body></html>
"""


def test_page_load_builds_dom_and_runs_inline_scripts_in_order():
    ctx = JSContext()
    page = Page(ctx)
    page.load(PAGE_HTML, baseURL="https://example.test/catalog/")
    assert ctx.eval("document.readyState") == "complete"
    assert ctx.eval(
        "document.getElementById('app').classList.contains('card')"
    ) is True
    assert ctx.eval("steps", to_py=True) == ["inline", "hello lv8"]
    # HTML5 lifecycle: the DOMContentLoaded handler observes "interactive";
    # "complete" only arrives after the load event
    assert ctx.eval("globalThis.domReady") == "interactive"
    # baseURL drives location/history state
    assert ctx.eval("location.href") == "https://example.test/catalog/"
    assert ctx.eval("history.length") == 1


def test_dom_queries_and_collections():
    ctx = JSContext()
    Page(ctx).load(PAGE_HTML)
    assert ctx.eval("document.getElementsByTagName('p').length") == 2
    assert ctx.eval("document.getElementsByClassName('note').length") == 2
    assert ctx.eval("document.querySelectorAll('.note').length") == 2
    assert (
        ctx.eval("document.querySelector('#app strong').textContent") == "lv8"
    )
    assert ctx.eval("document.querySelector('#missing')") is None
    # element-scoped queries
    assert ctx.eval("document.body.querySelector('.note').textContent") == "a"
    assert ctx.eval("document.body.querySelectorAll('p').length") == 2


def test_element_surface_and_to_string_tags():
    ctx = JSContext()
    Page(ctx).load(PAGE_HTML)
    assert ctx.eval("Object.prototype.toString.call(document)") == (
        "[object HTMLDocument]"
    )
    assert ctx.eval("Object.prototype.toString.call(document.body)") == (
        "[object HTMLBodyElement]"
    )
    assert ctx.eval("Object.prototype.toString.call(document.documentElement)")
    assert ctx.eval("document.documentElement.tagName") == "HTML"
    assert ctx.eval("document.body.tagName") == "BODY"
    assert ctx.eval("document.getElementById('app').innerHTML") == (
        "hello <strong>lv8</strong>"
    )
    assert ctx.eval(
        "document.getElementById('app').outerHTML"
    ).startswith("<div id=\"app\" class=\"card\">")
    # identity: same node wraps to the same JS object
    assert ctx.eval(
        "document.getElementById('app') === document.querySelector('#app')"
    ) is True
    assert ctx.eval(
        "document.body.parentElement === document.documentElement"
    ) is True


def test_window_document_is_getter_only_non_configurable_accessor():
    # reference answers gOPD(window, 'document') with an accessor whose
    # serialization is exactly {"enumerable":true,"configurable":false}
    # and whose getter is named "get document" (window-named-probe.json)
    ctx = JSContext()
    Page(ctx).load(PAGE_HTML, baseURL="https://example.test/catalog/")
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyDescriptor(window, 'document'))"
    ) == '{"enumerable":true,"configurable":false}'
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(window, 'document').get.name"
    ) == "get document"
    assert ctx.eval("window.document === document") is True
    # non-configurable semantics: sloppy-mode assignment and delete are
    # no-ops, while a page reload still swaps the wrapper through the
    # accessor's private slot
    assert ctx.eval("window.document = 1; window.document === document") is True
    assert ctx.eval("delete window.document") is False
    Page(ctx).load("<html><body><p id='p2'>second</p></body></html>")
    assert ctx.eval("document.getElementById('p2').textContent") == "second"
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyDescriptor(window, 'document'))"
    ) == '{"enumerable":true,"configurable":false}'
    # iframe realms carry the same accessor shape over their own
    # about:blank document
    ctx2 = JSContext()
    Page(ctx2).load(
        "<html><body><iframe name='fr'></iframe></body></html>"
    )
    assert ctx2.eval(
        "JSON.stringify(Object.getOwnPropertyDescriptor("
        "document.querySelector('iframe').contentWindow, 'document'))"
    ) == '{"enumerable":true,"configurable":false}'
    assert ctx2.eval(
        "document.querySelector('iframe').contentWindow.document"
        ".documentElement.tagName"
    ) == "HTML"


def test_embed_object_are_plain_elements_like_ref():
    # reference has no dedicated embed/object interfaces: the tag reads as
    # "[object Element]" with the chain Element->Node->EventTarget
    # (window-surface.json).  Window named access and form behavior ride
    # on tag names, not interfaces, and keep working.
    ctx = JSContext()
    Page(ctx).load(
        "<html><body><embed id='emb' name='emb'><object id='obj' name='obj'>"
        "<form id='f'><input name='q'></form></body></html>"
    )
    assert ctx.eval(
        "Object.prototype.toString.call(document.getElementById('emb'))"
    ) == "[object Element]"
    assert ctx.eval(
        "Object.prototype.toString.call(document.getElementById('obj'))"
    ) == "[object Element]"
    assert ctx.eval(
        "(function(){var o = document.getElementById('emb'), c = [];"
        "while (o) { c.push(Object.prototype.toString.call(o));"
        "o = Object.getPrototypeOf(o); } return c;})()",
        to_py=True,
    ) == [
        "[object Element]",
        "[object Node]",
        "[object EventTarget]",
        "[object Object]",
    ]
    assert ctx.eval("Object.prototype.toString.call(window.emb)") == (
        "[object Element]"
    )
    assert ctx.eval("Object.prototype.toString.call(window.obj)") == (
        "[object Element]"
    )
    assert ctx.eval("document.getElementById('f').length") == 1


def test_classlist_mutations_and_attribute_surface():
    ctx = JSContext()
    Page(ctx).load(
        "<html><body><div id='x' class='a b'></div></body></html>"
    )
    assert ctx.eval("document.getElementById('x').classList.contains('a')") is True
    ctx.eval("document.getElementById('x').classList.remove('a')")
    assert ctx.eval("document.getElementById('x').classList.contains('a')") is False
    assert ctx.eval("document.getElementById('x').classList.contains('b')") is True
    ctx.eval("document.getElementById('x').classList.add('c')")
    assert ctx.eval(
        "document.getElementById('x').getAttribute('class')"
    ) == "b c"
    ctx.eval("document.getElementById('x').setAttribute('data-x', '1')")
    assert ctx.eval("document.getElementById('x').getAttribute('data-x')") == "1"
    assert ctx.eval("document.getElementById('x').hasAttribute('data-x')") is True
    assert ctx.eval("document.getElementById('x').getAttribute('nope')") is None


def test_document_event_listener_table_survives_load():
    ctx = JSContext()
    Page(ctx).load(
        "<html><body><div id='app'>x</div>"
        "<script>globalThis.dcl = [];"
        "document.addEventListener('DOMContentLoaded', () => dcl.push('fired'));</script>"
        "</body></html>"
    )
    # inline-script listeners observe DOMContentLoaded during load
    assert ctx.eval("dcl", to_py=True) == ["fired"]
    # listeners registered after load keep working on the document target
    ctx.eval(
        "document.addEventListener('custom', () => dcl.push('custom'));"
        "document.dispatchEvent(new Event('custom'));"
    )
    assert ctx.eval("dcl", to_py=True) == ["fired", "custom"]


def test_g2_facade_page_is_context_bound():
    ctx = JSContext()
    page = Page(ctx)
    page.load("<html><body><b>bold</b></body></html>")
    assert ctx.eval("document.body.textContent") == "bold"
    # facade JSContext.load stays an error until G3 wires the resource table
    with pytest.raises(NotImplementedYetError):
        ctx.load("<html></html>")


def test_innerhtml_set_parses_fragment_and_replaces_children():
    # mirrors the oracle's test_dynamic_dom_html_parsing_cloning_and_
    # serialization: fragment parsing + exact serialization format
    ctx = JSContext()
    Page(ctx).load("<html><body><main id='root'><p>old</p></main></body></html>")
    # detached children stay readable, like browsers (capture before the set)
    ctx.eval("globalThis.old = document.querySelector('#root p')")
    ctx.eval(
        "document.getElementById('root').innerHTML = "
        "'<section class=\"panel\"><span>one</span></section>'"
    )
    assert ctx.eval("document.getElementById('root').innerHTML") == (
        '<section class="panel"><span>one</span></section>'
    )
    assert ctx.eval("document.querySelector('#root .panel span').textContent") == (
        "one"
    )
    assert ctx.eval("old.textContent") == "old"
    assert ctx.eval("old.parentElement") is None
    # a second set replaces the first fragment's children
    ctx.eval("document.getElementById('root').innerHTML = '<em>x</em>'")
    assert ctx.eval("document.querySelector('#root .panel')") is None
    assert ctx.eval("document.getElementById('root').innerHTML") == "<em>x</em>"


def test_create_append_remove_node_moves_and_identity():
    ctx = JSContext()
    Page(ctx).load("<html><body><main id='root'><p>old</p></main></body></html>")
    # createElement normalizes the tag name like HTML documents do
    ctx.eval(
        "globalThis.d = document.createElement('DIV');"
        "d.id = 'added';"
        "document.body.appendChild(d);"
    )
    assert ctx.eval("d.tagName") == "DIV"
    # node identity survives insertion and queries (wrap cache)
    assert ctx.eval("document.getElementById('added') === d") is True
    assert ctx.eval("document.getElementsByTagName('div').length") == 1
    # text nodes attach through appendChild
    ctx.eval("d.appendChild(document.createTextNode('hi'))")
    assert ctx.eval("document.getElementById('added').textContent") == "hi"
    assert ctx.eval("document.getElementById('added').innerHTML") == "hi"
    # removeChild detaches the subtree but keeps it readable
    ctx.eval(
        "globalThis.removed ="
        "document.getElementById('root').removeChild("
        "document.querySelector('#root > *'));"
    )
    assert ctx.eval(
        "document.getElementById('root').innerHTML"
    ) == ""
    assert ctx.eval("removed.textContent") == "old"
    # appending an attached node moves it (browser semantics)
    ctx.eval("document.getElementById('root').appendChild(d)")
    assert ctx.eval("document.querySelector('main > div') === d") is True
    assert ctx.eval("document.getElementById('added').textContent") == "hi"
    assert ctx.eval(
        "document.getElementById('root').innerHTML"
    ) == '<div id="added">hi</div>'


def test_collections_expose_browser_node_list_types():
    # mirrors the oracle's collection-type test: queries return real
    # NodeList/HTMLCollection instances with item()/namedItem()/forEach
    ctx = JSContext()
    Page(ctx).load(
        "<html><body><div id='root'><span>a</span><span>b</span></div>"
        "<form id='f'></form></body></html>"
    )
    assert ctx.eval(
        "[document.getElementById('root').querySelectorAll('span') "
        "instanceof NodeList, "
        "document.getElementById('root').querySelectorAll('span').length, "
        "document.getElementById('root').querySelectorAll('span')"
        ".item(1).textContent]",
        to_py=True,
    ) == [True, 2, "b"]
    assert ctx.eval(
        "[document.getElementById('root').childNodes instanceof NodeList,"
        "document.getElementById('root').childNodes.length,"
        "document.getElementById('root').childNodes.item(2)]",
        to_py=True,
    ) == [True, 2, None]
    # G4.331 reference ground truth: document.forms is ALWAYS null there (even
    # with <form> elements on the page) - the HTMLCollection return was an
    # lv8 invention
    assert ctx.eval("document.forms === null") is True
    # readable-but-not-array collections: indexed reads + browser tags
    assert ctx.eval(
        "document.getElementsByTagName('span')[1].textContent"
    ) == "b"
    assert ctx.eval(
        "Object.prototype.toString.call(document.getElementsByTagName('span'))"
    ) == "[object HTMLCollection]"
    assert ctx.eval(
        "Object.prototype.toString.call("
        "document.getElementById('root').childNodes)"
    ) == "[object NodeList]"
    # element-scoped by-tag returns an HTMLCollection rooted at the element
    assert ctx.eval(
        "[document.getElementById('root').getElementsByTagName('span').length,"
        "document.getElementById('root').getElementsByTagName('div').length]",
        to_py=True,
    ) == [2, 0]
    # forEach visits child nodes in document order
    assert ctx.eval(
        "(function(){let acc=[];"
        "document.getElementById('root').childNodes.forEach("
        "function(n){acc.push(n.textContent)});"
        "return acc.join('|')})()"
    ) == "a|b"


def test_insert_adjacent_html_clone_node_dom_parser_xml_serializer():
    # the oracle's test_dynamic_dom_html_parsing_cloning_and_serialization
    # verbatim: fragment writes, positional insert, deep clone, standalone
    # document parse + serialization
    ctx = JSContext()
    Page(ctx).load(
        "<html><body><main id='root'><p>old</p></main></body></html>"
    )
    ctx.eval(
        """
        const root = document.querySelector('#root');
        root.innerHTML = '<section class="panel"><span>one</span></section>';
        root.querySelector('span').insertAdjacentHTML('afterend', '<strong data-id="2">two</strong>');
        const clone = root.cloneNode(true);
        globalThis.domValues = [root.querySelectorAll('.panel span, .panel strong').length, clone.textContent, root.innerHTML];
        const parsed = new DOMParser().parseFromString('<article id="parsed"><em>value</em></article>', 'text/html');
        globalThis.serialized = new XMLSerializer().serializeToString(parsed.querySelector('#parsed'));
        """
    )
    assert ctx.eval("domValues", to_py=True) == [
        2,
        "onetwo",
        '<section class="panel"><span>one</span>'
        '<strong data-id="2">two</strong></section>',
    ]
    assert ctx.eval("serialized") == (
        '<article id="parsed"><em>value</em></article>'
    )
    # DOMParser documents are real documents (complete, full surface);
    # the reference tags them [object Document] (M6: the page document
    # stays [object HTMLDocument] - see work/reference-traces/_m6_dyntag.json)
    assert ctx.eval("Object.prototype.toString.call(parsed)") == (
        "[object Document]"
    )
    assert ctx.eval("parsed.readyState") == "complete"
    assert ctx.eval("parsed.querySelector('em').textContent") == "value"
    # all four insert positions, against an isolated host
    ctx.eval(
        """
        const host = document.createElement('div');
        host.innerHTML = '<i>x</i>';
        const b = host.firstElementChild;
        b.insertAdjacentHTML('beforebegin', '<b>1</b>');
        b.insertAdjacentHTML('afterbegin', '<u>2</u>');
        b.insertAdjacentHTML('beforeend', '<s>3</s>');
        b.insertAdjacentHTML('afterend', '<em>4</em>');
        globalThis.positions = host.innerHTML;
        """
    )
    assert ctx.eval("positions") == (
        "<b>1</b><i><u>2</u>x<s>3</s></i><em>4</em>"
    )
    # shallow clone copies the element but no children
    ctx.eval("globalThis.shallow = document.querySelector('main').cloneNode(false)")
    assert ctx.eval("shallow.tagName") == "MAIN"
    assert ctx.eval("shallow.childNodes.length") == 0


def test_document_write_persists_nodes_without_reexecution():
    # oracle shape: parse-time document.write lands in the document and the
    # running script is not re-executed
    ctx = JSContext()
    Page(ctx).load(
        html=(
            "<body><script>globalThis.writeCount = "
            "(globalThis.writeCount || 0) + 1; "
            "document.write('<div id=written>saved</div>')</script></body>"
        ),
        baseURL="https://example.test/index.html",
    )
    assert ctx.eval("writeCount") == 1
    assert ctx.eval(
        "document.getElementById('written').textContent"
    ) == "saved"
    # writes inside one script keep document order; writeln adds the newline
    ctx2 = JSContext()
    Page(ctx2).load(
        "<html><body><script>document.write(1); document.write(2); "
        "document.writeln(3)</script></body></html>"
    )
    assert ctx2.eval("document.body.textContent") == (
        "document.write(1); document.write(2); document.writeln(3)123\n"
    )
    # post-load writes append to the body end
    ctx2.eval("document.write('<i id=late>tail</i>')")
    assert ctx2.eval("document.getElementById('late').textContent") == "tail"


def test_document_ranges_select_clone_delete_and_insert_dom_content():
    # the oracle's Range test verbatim: child-index boundaries, content
    # cloning/removal, collapse and text-node split insertion
    ctx = JSContext()
    Page(ctx).load(
        "<div id='root'>hello <strong>lv8</strong> world</div>"
    )
    ctx.eval(
        """
        const root = document.getElementById('root');
        const range = document.createRange(); range.setStart(root, 1); range.setEnd(root, 2);
        globalThis.rangeInfo = [range.toString(), range.cloneContents().textContent, range.commonAncestorContainer.id];
        range.deleteContents();
        const text = root.firstChild; const insert = document.createRange(); insert.setStart(text, 5); insert.collapse(true); insert.insertNode(document.createElement('em')); root.querySelector('em').textContent = '!';
        """
    )
    assert ctx.eval("rangeInfo", to_py=True) == ["lv8", "lv8", "root"]
    assert ctx.eval(
        "document.getElementById('root').textContent"
    ) == "hello!  world"
    assert ctx.eval(
        "document.getElementById('root').innerHTML"
    ) == "hello<em>!</em>  world"
    # text-offset boundaries substring inside one text node
    ctx2 = JSContext()
    Page(ctx2).load("<div id='r'>abcdef</div>")
    assert ctx2.eval(
        "const g = document.createRange();"
        "g.setStart(document.getElementById('r').firstChild, 1);"
        "g.setEnd(document.getElementById('r').firstChild, 4);"
        "g.toString()"
    ) == "bcd"
    # same-container text deletion edits the text node data
    ctx2.eval("g.deleteContents()")
    assert ctx2.eval("document.getElementById('r').textContent") == "aef"


def test_custom_elements_and_open_shadow_roots_mount_inside_the_dom():
    # the oracle's custom elements + shadow DOM test verbatim
    ctx = JSContext()
    Page(ctx).load("<body></body>")
    ctx.eval(
        """
        class Lv8Card extends HTMLElement {
          constructor() { super(); this.created = true; }
          connectedCallback() { this.connected = true; }
        }
        customElements.define('lv8-card', Lv8Card);
        const card = document.createElement('lv8-card');
        document.body.appendChild(card);
        const root = card.attachShadow({mode: 'open', delegatesFocus: true});
        const label = document.createElement('span'); label.className = 'label'; label.textContent = 'inside'; root.appendChild(label);
        globalThis.customInfo = [card.created, card.connected, card.tagName, card.shadowRoot === root, root.host === card, root.delegatesFocus, root.querySelector('.label').textContent];
        """
    )
    assert ctx.eval("customInfo", to_py=True) == [
        True,
        True,
        "LV8-CARD",
        True,
        True,
        True,
        "inside",
    ]
    assert ctx.eval("customElements.get('lv8-card') !== undefined") is True
    # shadow content stays outside the light DOM
    assert ctx.eval("card.textContent") == ""
    assert ctx.eval(
        "root.querySelector('.label') === root.childNodes[0]"
    ) is True


def test_custom_element_definitions_upgrade_already_parsed_elements():
    # the oracle's upgrade test verbatim: define() rebuilds wrappers for
    # already-parsed elements (identity changes) and runs the lifecycle
    ctx = JSContext()
    Page(ctx).load(
        '<x-status id="status" data-state="ready"><span>content</span></x-status>'
    )
    ctx.eval(
        """
        let connections = 0;
        class XStatus extends HTMLElement {
          constructor() { super(); this.constructed = true; }
          connectedCallback() { connections++; this.connectedText = this.textContent; }
        }
        const before = document.getElementById('status');
        customElements.define('x-status', XStatus);
        const after = document.getElementById('status');
        globalThis.upInfo = [before === after, after instanceof XStatus, after.constructed, after.getAttribute('data-state'), after.connectedText, connections, customElements.get('x-status') === XStatus];
        """
    )
    assert ctx.eval("upInfo", to_py=True) == [
        False,
        True,
        True,
        "ready",
        "content",
        1,
        True,
    ]


def test_css_selector_subset_combinators_attributes_pseudo_classes():
    # the lexbor-backed selector engine covers the browser subset the
    # oracle probes rely on: combinators, attribute operators, pseudo-classes
    ctx = JSContext()
    Page(ctx).load(
        '<html><body>'
        '<ul id="list"><li class="item on" data-v="1">a</li>'
        '<li class="item" data-v="2">b</li><li data-v="3">c</li></ul>'
        '<div id="wrap"><p>one</p><span>x</span><p>two</p></div>'
        '<a id="l1" href="https://ex.test/a">1</a><a id="l2">2</a>'
        '<form><input id="i1" type="text" disabled>'
        '<input id="i2" type="checkbox" checked></form>'
        '</body></html>'
    )
    # combinator selectors (descendant / child / adjacent / general sibling)
    assert ctx.eval(
        "[document.querySelectorAll('#list > li.item').length,"
        "document.querySelectorAll('#list .item + li').length,"
        "document.querySelectorAll('#list li ~ li').length,"
        "document.querySelectorAll('#wrap p').length]",
        to_py=True,
    ) == [2, 2, 2, 2]
    # attribute operators: equals / prefix / suffix / contains / word / exists
    assert ctx.eval(
        "[document.querySelectorAll('li[data-v=\"2\"]').length,"
        "document.querySelectorAll('li[data-v^=\"1\"]').length,"
        "document.querySelectorAll('li[data-v$=\"3\"]').length,"
        "document.querySelectorAll('li[data-v*=\"2\"]').length,"
        "document.querySelectorAll('li[data-v~=\"1\"]').length,"
        "document.querySelectorAll('a[href]').length]",
        to_py=True,
    ) == [1, 1, 1, 1, 1, 1]
    # pseudo-classes
    assert ctx.eval(
        "[document.querySelectorAll('li:first-child').length,"
        "document.querySelectorAll('li:last-child').length,"
        "document.querySelectorAll('li:nth-child(2)').length,"
        "document.querySelectorAll('li:not(.on)').length,"
        "document.querySelectorAll('#wrap p:nth-of-type(2)').length,"
        "document.querySelectorAll('input:disabled').length,"
        "document.querySelectorAll('input:checked').length,"
        "document.querySelectorAll(':root').length]",
        to_py=True,
    ) == [1, 1, 1, 2, 1, 1, 1, 1]
    # invalid selectors throw SyntaxError like browsers, matching nothing
    with pytest.raises(Exception) as exc:
        ctx.eval("document.querySelectorAll('ul >> li')")
    assert "SyntaxError" in str(exc.value)
    assert ctx.eval("document.querySelector('#missing')") is None


def test_node_list_is_iterable_but_html_collection_is_not():
    ctx = JSContext()
    Page(ctx).load('<html><body><div id="w"><p>one</p><p>two</p></div></body></html>')
    # for-of walks the NodeList in document order
    assert ctx.eval(
        "(function(){ let out = [];"
        "for (const p of document.querySelectorAll('#w p')) out.push(p.textContent);"
        "return out })()",
        to_py=True,
    ) == ["one", "two"]
    # spread works through Symbol.iterator
    assert ctx.eval(
        "[...document.querySelectorAll('#w p')].length", to_py=True
    ) == 2
    # HTMLCollection is iterable too - the reference wheel exposes
    # Symbol.iterator (values) on its prototype (M8: reference ground truth,
    # work/reference-traces/_m8_protoface.json)
    assert ctx.eval(
        "Symbol.iterator in Object.getPrototypeOf("
        "document.getElementsByTagName('p'))",
        to_py=True,
    ) is True
