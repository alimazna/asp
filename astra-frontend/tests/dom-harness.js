/* A minimal, dependency-free DOM + fetch harness for the ASTRA degraded-path
   integration tests.

   It is deliberately small: ASTRA's app.js uses a narrow slice of the DOM
   (createElement, createTextNode, getElementById, querySelectorAll, dataset,
   className, classList, addEventListener, focus) plus fetch/AbortController and
   a canvas 2d context. This harness implements exactly that slice so a view
   can be rendered in Node and its text/classes asserted — no jsdom, no browser.

   Every stubbed response is clearly test-only. Nothing here is production. */

"use strict";

/* --------------------------------------------------------------- DOM nodes */

class ClassList {
  constructor() { this._set = new Set(); }
  add(...names) { names.forEach((n) => n && this._set.add(String(n))); }
  remove(...names) { names.forEach((n) => this._set.delete(String(n))); }
  contains(name) { return this._set.has(String(name)); }
  toggle(name, force) {
    const on = force === undefined ? !this.contains(name) : Boolean(force);
    if (on) this.add(name); else this.remove(name);
    return on;
  }
  toString() { return Array.from(this._set).join(" "); }
}

class El {
  constructor(tag) {
    this.tagName = String(tag || "").toUpperCase();
    this.children = [];
    this.childNodes = this.children;
    this.parentNode = null;
    this.attributes = {};
    this.dataset = {};
    this._text = "";
    this._listeners = {};
    this.style = {};
    this._classList = new ClassList();
  }

  get className() { return this._classList.toString(); }
  set className(v) {
    this._classList = new ClassList();
    String(v || "").split(/\s+/).forEach((c) => c && this._classList.add(c));
  }
  get classList() { return this._classList; }

  setAttribute(name, value) {
    if (name === "class") { this.className = value; return; }
    this.attributes[name] = String(value);
    if (name === "id") this.id = String(value);
    if (name.startsWith("data-")) {
      this.dataset[name.slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase())] = String(value);
    }
  }
  getAttribute(name) { return this.attributes[name]; }
  removeAttribute(name) { delete this.attributes[name]; }
  hasAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attributes, name); }

  appendChild(child) {
    if (!child) return child;
    child.parentNode = this;
    this.children.push(child);
    return child;
  }
  removeChild(child) {
    const i = this.children.indexOf(child);
    if (i >= 0) this.children.splice(i, 1);
    if (child) child.parentNode = null;
    return child;
  }
  get firstChild() { return this.children.length ? this.children[0] : null; }
  get parentElement() { return this.parentNode; }

  addEventListener(ev, fn) {
    (this._listeners[ev] = this._listeners[ev] || []).push(fn);
  }
  removeEventListener(ev, fn) {
    const list = this._listeners[ev];
    if (!list) return;
    const i = list.indexOf(fn);
    if (i >= 0) list.splice(i, 1);
  }
  dispatch(ev, arg) { (this._listeners[ev] || []).forEach((fn) => fn(arg || {})); }

  focus() {}
  blur() {}

  get textContent() {
    if (this.children.length === 0) return this._text;
    return this.children.map((c) => c.textContent).join("");
  }
  set textContent(v) {
    this.children.length = 0;
    this._text = v === null || v === undefined ? "" : String(v);
  }
  get innerHTML() { return this._innerHTML || ""; }
  set innerHTML(v) { this._innerHTML = String(v === null || v === undefined ? "" : v); }

  get clientWidth() { return 320; }
  get clientHeight() { return 320; }
  getContext() { return new Ctx2d(); }

  querySelector(sel) {
    const list = this.querySelectorAll(sel);
    return list.length ? list[0] : null;
  }
  querySelectorAll(sel) {
    const out = [];
    this.walk((el) => { if (el !== this && matches(el, String(sel || "").trim())) out.push(el); });
    return out;
  }

  /* Depth-first walk over this node and every descendant. */
  walk(visit) {
    visit(this);
    this.children.forEach((c) => c.walk(visit));
  }
}

class Ctx2d {
  constructor() {
    this.fillStyle = "";
    this.strokeStyle = "";
    this.font = "";
    this.textAlign = "";
    this.textBaseline = "";
    this.lineWidth = 1;
  }
  setTransform() {}
  clearRect() {}
  fillText() {}
  beginPath() {}
  moveTo() {}
  lineTo() {}
  stroke() {}
  fillRect() {}
}

/* --------------------------------------------------------------- document */

function createDocument() {
  const byId = new Map();
  const all = [];

  const doc = {
    // "loading" so app.js registers a DOMContentLoaded handler instead of
    // booting the full shell; the tests render individual views instead.
    readyState: "loading",
    hidden: false,
    title: "",
    _listeners: {},
    documentElement: null,
    body: null,
    createElement(tag) { const el = new El(tag); all.push(el); return el; },
    createTextNode(text) {
      const el = new El("#text");
      el._text = text === null || text === undefined ? "" : String(text);
      return el;
    },
    getElementById(id) {
      if (byId.has(id)) return byId.get(id);
      for (const el of all) { if (el.id === id) return el; }
      return null;
    },
    querySelector(sel) {
      const list = doc.querySelectorAll(sel);
      return list.length ? list[0] : null;
    },
    querySelectorAll(sel) {
      const out = [];
      const simple = String(sel || "").trim();
      all.forEach((el) => {
        if (matches(el, simple)) out.push(el);
      });
      return out;
    },
    addEventListener(ev, fn) { (doc._listeners[ev] = doc._listeners[ev] || []).push(fn); },
    removeEventListener(ev, fn) {
      const list = doc._listeners[ev];
      if (!list) return;
      const i = list.indexOf(fn);
      if (i >= 0) list.splice(i, 1);
    },
    dispatch(ev, arg) { (doc._listeners[ev] || []).forEach((fn) => fn(arg || {})); },
    _register(el, id) { byId.set(id, el); },
  };

  doc.documentElement = doc.createElement("html");
  doc.body = doc.createElement("body");
  return doc;
}

/* Supports "#id", ".class", "tag", and the single compound selector
   "#sidebar .nav__item" used by app.js. Good enough for the views under test. */
function matches(el, sel) {
  if (!sel) return false;
  if (sel.includes(" ")) {
    const parts = sel.trim().split(/\s+/);
    // Only "ancestor descendant" is used; match the descendant and check the
    // ancestors by walking the parent chain.
    const target = parts[parts.length - 1];
    if (!matches(el, target)) return false;
    const ancestorSel = parts.slice(0, -1).join(" ");
    let p = el.parentNode;
    while (p) { if (matches(p, ancestorSel)) return true; p = p.parentNode; }
    return false;
  }
  if (sel.startsWith("#")) return el.id === sel.slice(1);
  if (sel.startsWith(".")) return el.classList.contains(sel.slice(1));
  return el.tagName === sel.toUpperCase();
}

/* --------------------------------------------------------------- globals */

/* A fetch stub. `routes` maps a path prefix to { status, json }. A matched
   route returns a Response-like object; anything else rejects, so the view's
   "unreachable" path is exercised too. */
function install(globalObj, opts) {
  const options = opts || {};
  const doc = createDocument();
  const routes = options.routes || {};
  const calls = [];
  const storage = new Map();

  const win = {
    devicePixelRatio: 1,
    innerWidth: options.innerWidth || 1280,
    _listeners: {},
    addEventListener(ev, fn) { (win._listeners[ev] = win._listeners[ev] || []).push(fn); },
    removeEventListener() {},
    matchMedia() { return { matches: false, addEventListener() {}, removeEventListener() {} }; },
  };

  const localStorage = {
    getItem(k) { return storage.has(k) ? storage.get(k) : null; },
    setItem(k, v) { storage.set(k, String(v)); },
    removeItem(k) { storage.delete(k); },
  };

  function fetchStub(url, init) {
    calls.push({ url: url, init: init });
    const match = Object.keys(routes).find((prefix) => String(url).includes(prefix));
    if (!match) {
      return Promise.reject(new Error("no route for " + url));
    }
    const r = routes[match];
    const body = typeof r.json === "function" ? r.json(url) : r.json;
    return Promise.resolve({
      ok: r.status < 400,
      status: r.status,
      json: () => Promise.resolve(body),
    });
  }

  class AbortControllerStub {
    constructor() { this.signal = {}; }
    abort() {}
  }

  win.ASTRA = globalObj.ASTRA;
  win.ASTRA_BACKEND_BASE = options.backendBase === undefined
    ? "https://backend.test" : options.backendBase;
  win.ASTRA_REFRESH_MS = 0;
  win.ASTRA_ALLOW_SAME_ORIGIN = false;

  const sandbox = {
    window: win,
    document: doc,
    localStorage: localStorage,
    fetch: fetchStub,
    AbortController: AbortControllerStub,
    setTimeout: setTimeout,
    clearTimeout: clearTimeout,
    setInterval: () => 0,
    clearInterval: () => {},
    Date: Date,
    console: console,
    getComputedStyle: () => ({ getPropertyValue: () => "" }),
    location: { hash: "#/dashboard" },
    history: {},
    navigator: { clipboard: { writeText: () => Promise.resolve() } },
  };

  /* config.js runs first in the real page; emulate its effect. */
  win.__astra_calls = calls;
  return {
    window: win,
    document: doc,
    localStorage: localStorage,
    sandbox: sandbox,
    calls: calls,
    register(id, el) { doc._register(el, id); },
  };
}

module.exports = { install: install, El: El };
