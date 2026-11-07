// node_modules/hono/dist/request/constants.js
var GET_MATCH_RESULT = Symbol();

// node_modules/hono/dist/utils/buffer.js
var bufferToFormData = (arrayBuffer, contentType) => {
  return new Response(arrayBuffer, { headers: { "Content-Type": contentType.replace(/^[^;]+/, (mediaType) => mediaType.toLowerCase()) } }).formData();
};

// node_modules/hono/dist/utils/body.js
var MAX_NESTED_OBJECTS = 1e4;
var isRawRequest = (request) => "headers" in request;
var parseBody = async (request, options = /* @__PURE__ */ Object.create(null)) => {
  const { all = false, dot = false } = options;
  const mediaType = (isRawRequest(request) ? request.headers : request.raw.headers).get("Content-Type")?.split(";")[0].trim().toLowerCase();
  if (mediaType === "multipart/form-data" || mediaType === "application/x-www-form-urlencoded") return parseFormData(request, {
    all,
    dot
  });
  return {};
};
async function parseFormData(request, options) {
  if (!isRawRequest(request) && request.bodyCache.formData) return convertFormDataToBodyData(await request.bodyCache.formData, options);
  const headers = isRawRequest(request) ? request.headers : request.raw.headers;
  const arrayBuffer = await request.arrayBuffer();
  const formDataPromise = bufferToFormData(arrayBuffer, headers.get("Content-Type") || "");
  if (!isRawRequest(request)) request.bodyCache.formData = formDataPromise;
  const formData = await formDataPromise;
  if (formData) return convertFormDataToBodyData(formData, options);
  return {};
}
function convertFormDataToBodyData(formData, options) {
  const form = /* @__PURE__ */ Object.create(null);
  const nestingState = { count: 0 };
  formData.forEach((value, key) => {
    if (!(options.all || key.endsWith("[]"))) form[key] = value;
    else handleParsingAllValues(form, key, value);
  });
  if (options.dot) Object.entries(form).forEach(([key, value]) => {
    if (key.includes(".")) {
      handleParsingNestedValues(form, key, value, nestingState);
      delete form[key];
    }
  });
  return form;
}
var handleParsingAllValues = (form, key, value) => {
  if (form[key] !== void 0) {
    if (Array.isArray(form[key])) form[key].push(value);
    else form[key] = [form[key], value];
  } else if (!key.endsWith("[]")) form[key] = value;
  else form[key] = [value];
};
var handleParsingNestedValues = (form, key, value, state) => {
  if (/(?:^|\.)__proto__\./.test(key)) return;
  let nestedForm = form;
  const keys = key.split(".", 34);
  if (keys.length > 33) throwNestingLimitExceeded();
  keys.forEach((key2, index) => {
    if (index === keys.length - 1) nestedForm[key2] = value;
    else {
      if (!nestedForm[key2] || typeof nestedForm[key2] !== "object" || Array.isArray(nestedForm[key2]) || nestedForm[key2] instanceof File) {
        if (state.count++ >= MAX_NESTED_OBJECTS) throwNestingLimitExceeded();
        nestedForm[key2] = /* @__PURE__ */ Object.create(null);
      }
      nestedForm = nestedForm[key2];
    }
  });
};
var throwNestingLimitExceeded = () => {
  throw new Error("Nesting limit exceeded");
};

// node_modules/hono/dist/utils/url.js
var splitPath = (path) => {
  const paths = path.split("/");
  if (paths[0] === "") paths.shift();
  return paths;
};
var splitRoutingPath = (routePath) => {
  const { groups, path } = extractGroupsFromPath(routePath);
  const paths = splitPath(path);
  return replaceGroupMarks(paths, groups);
};
var extractGroupsFromPath = (path) => {
  const groups = [];
  path = path.replace(/\{[^}]+\}/g, (match2, index) => {
    const mark = `@${index}`;
    groups.push([mark, match2]);
    return mark;
  });
  return {
    groups,
    path
  };
};
var replaceGroupMarks = (paths, groups) => {
  for (let i = groups.length - 1; i >= 0; i--) {
    const [mark] = groups[i];
    for (let j = paths.length - 1; j >= 0; j--) if (paths[j].includes(mark)) {
      paths[j] = paths[j].replace(mark, groups[i][1]);
      break;
    }
  }
  return paths;
};
var patternCache = {};
var getPattern = (label, next) => {
  if (label === "*") return "*";
  const match2 = label.match(/^\:([^\{\}]+)(?:\{(.+)\})?$/);
  if (match2) {
    const cacheKey = `${label}#${next}`;
    if (!patternCache[cacheKey]) {
      if (match2[2]) patternCache[cacheKey] = next && next[0] !== ":" && next[0] !== "*" ? [
        cacheKey,
        match2[1],
        new RegExp(`^${match2[2]}(?=/${next})`)
      ] : [
        label,
        match2[1],
        new RegExp(`^${match2[2]}$`)
      ];
      else patternCache[cacheKey] = [
        label,
        match2[1],
        true
      ];
    }
    return patternCache[cacheKey];
  }
  return null;
};
var tryDecode = (str, decoder2) => {
  try {
    return decoder2(str);
  } catch {
    return str.replace(/(?:%[0-9A-Fa-f]{2})+/g, (match2) => {
      try {
        return decoder2(match2);
      } catch {
        return match2;
      }
    });
  }
};
var tryDecodeURI = (str) => tryDecode(str, decodeURI);
var getPath = (request) => {
  const url = request.url;
  const start = url.indexOf("/", url.indexOf(":") + 4);
  let i = start;
  for (; i < url.length; i++) {
    const charCode = url.charCodeAt(i);
    if (charCode === 37) {
      const queryIndex = url.indexOf("?", i);
      const hashIndex = url.indexOf("#", i);
      const end = queryIndex === -1 ? hashIndex === -1 ? void 0 : hashIndex : hashIndex === -1 ? queryIndex : Math.min(queryIndex, hashIndex);
      const path = url.slice(start, end);
      return tryDecodeURI(path.includes("%25") ? path.replace(/%25/g, "%2525") : path);
    } else if (charCode === 63 || charCode === 35) break;
  }
  return url.slice(start, i);
};
var getPathNoStrict = (request) => {
  const result = getPath(request);
  return result.length > 1 && result.at(-1) === "/" ? result.slice(0, -1) : result;
};
var stripPathPrefix = (rawPath, decodedPath, prefixLength) => {
  if (rawPath.at(-1) === "/" && decodedPath.at(-1) !== "/") rawPath = rawPath.slice(0, -1);
  let index = 0;
  for (let i = decodedPath.indexOf("/"); i !== -1 && i < prefixLength; i = decodedPath.indexOf("/", i + 1)) {
    index = rawPath.indexOf("/", index + 1);
    if (index === -1) return "/";
  }
  return rawPath.slice(index) || "/";
};
var mergePath = (base, sub, ...rest) => {
  if (rest.length) sub = mergePath(sub, ...rest);
  return `${base?.[0] === "/" ? "" : "/"}${base}${sub === "/" ? "" : `${base?.at(-1) === "/" ? "" : "/"}${sub?.[0] === "/" ? sub.slice(1) : sub}`}`;
};
var checkOptionalParameter = (path) => {
  if (path.charCodeAt(path.length - 1) !== 63 || !path.includes(":")) return null;
  const segments = path.split("/");
  const results = [];
  let basePath = "";
  segments.forEach((segment) => {
    if (segment !== "" && !/\:/.test(segment)) basePath += "/" + segment;
    else if (/\:/.test(segment)) {
      if (segment.charCodeAt(segment.length - 1) === 63) {
        if (results.length === 0 && basePath === "") results.push("/");
        else results.push(basePath);
        const optionalSegment = segment.slice(0, -1);
        basePath += "/" + optionalSegment;
        results.push(basePath);
      } else basePath += "/" + segment;
    }
  });
  return results.filter((v, i, a) => a.indexOf(v) === i);
};
var tryDecodeURIComponent = (str) => str.indexOf("%") !== -1 ? tryDecode(str, decodeURIComponent_) : str;
var _decodeURI = (value) => {
  if (value.indexOf("+") !== -1) value = value.replace(/\+/g, " ");
  return tryDecodeURIComponent(value);
};
var _getQueryParam = (url, key, multiple) => {
  const hashIndex = url.indexOf("#", 8);
  if (hashIndex !== -1) url = url.slice(0, hashIndex);
  let encoded;
  if (!multiple && key && key.indexOf("%") === -1 && key.indexOf("+") === -1) {
    const queryIndex = url.indexOf("?", 8);
    let keyIndex2 = queryIndex;
    if (keyIndex2 === -1) return;
    if (!url.startsWith(key, keyIndex2 + 1)) keyIndex2 = url.indexOf(`&${key}`, keyIndex2 + 1);
    while (keyIndex2 !== -1) {
      const trailingKeyCode = url.charCodeAt(keyIndex2 + key.length + 1);
      if (trailingKeyCode === 61 || trailingKeyCode === 38 || isNaN(trailingKeyCode)) {
        if (keyIndex2 > queryIndex && /(?:^|&)[^=&]*[%+]/.test(url.slice(queryIndex + 1, keyIndex2))) break;
        if (trailingKeyCode === 61) {
          const valueIndex = keyIndex2 + key.length + 2;
          const endIndex = url.indexOf("&", valueIndex);
          return _decodeURI(url.slice(valueIndex, endIndex === -1 ? void 0 : endIndex));
        }
        return "";
      }
      keyIndex2 = url.indexOf(`&${key}`, keyIndex2 + 1);
    }
    encoded = /[%+]/.test(url);
    if (!encoded) return;
  }
  const results = /* @__PURE__ */ Object.create(null);
  encoded ??= /[%+]/.test(url);
  let keyIndex = url.indexOf("?", 8);
  while (keyIndex !== -1) {
    const nextKeyIndex = url.indexOf("&", keyIndex + 1);
    let valueIndex = url.indexOf("=", keyIndex);
    if (valueIndex > nextKeyIndex && nextKeyIndex !== -1) valueIndex = -1;
    let name = url.slice(keyIndex + 1, valueIndex === -1 ? nextKeyIndex === -1 ? void 0 : nextKeyIndex : valueIndex);
    if (encoded) name = _decodeURI(name);
    keyIndex = nextKeyIndex;
    if (name === "") continue;
    let value;
    if (valueIndex === -1) value = "";
    else {
      value = url.slice(valueIndex + 1, nextKeyIndex === -1 ? void 0 : nextKeyIndex);
      if (encoded) value = _decodeURI(value);
    }
    if (multiple) {
      if (!(results[name] && Array.isArray(results[name]))) results[name] = [];
      results[name].push(value);
    } else results[name] ??= value;
  }
  return key ? results[key] : results;
};
var getQueryParam = _getQueryParam;
var getQueryParams = (url, key) => {
  return _getQueryParam(url, key, true);
};
var decodeURIComponent_ = decodeURIComponent;

// node_modules/hono/dist/request.js
var HonoRequest = class {
  /**
  * `.raw` can get the raw Request object.
  *
  * @see {@link https://hono.dev/docs/api/request#raw}
  *
  * @example
  * ```ts
  * // For Cloudflare Workers
  * app.post('/', async (c) => {
  *   const metadata = c.req.raw.cf?.hostMetadata?
  *   ...
  * })
  * ```
  */
  raw;
  #validatedData;
  #matchResult;
  routeIndex = 0;
  /**
  * `.path` can get the pathname of the request.
  *
  * @see {@link https://hono.dev/docs/api/request#path}
  *
  * @example
  * ```ts
  * app.get('/about/me', (c) => {
  *   const pathname = c.req.path // `/about/me`
  * })
  * ```
  */
  path;
  bodyCache = {};
  constructor(request, path = "/", matchResult = [[]]) {
    this.raw = request;
    this.path = path;
    this.#matchResult = matchResult;
  }
  param(key) {
    return key ? this.#getDecodedParam(key) : this.#getAllDecodedParams();
  }
  #getDecodedParam(key) {
    const paramKey = this.#matchResult[0][this.routeIndex]?.[1][key];
    const param = this.#getParamValue(paramKey);
    return param && tryDecodeURIComponent(param);
  }
  #getAllDecodedParams() {
    const decoded = {};
    const keys = Object.keys(this.#matchResult[0][this.routeIndex]?.[1] ?? {});
    for (const key of keys) {
      const value = this.#getParamValue(this.#matchResult[0][this.routeIndex][1][key]);
      if (value !== void 0) decoded[key] = tryDecodeURIComponent(value);
    }
    return decoded;
  }
  #getParamValue(paramKey) {
    return this.#matchResult[1] ? this.#matchResult[1][paramKey] : paramKey;
  }
  query(key) {
    return getQueryParam(this.url, key);
  }
  queries(key) {
    return getQueryParams(this.url, key);
  }
  header(name) {
    if (name) return this.raw.headers.get(name) ?? void 0;
    const headerData = /* @__PURE__ */ Object.create(null);
    this.raw.headers.forEach((value, key) => {
      headerData[key] = value;
    });
    return headerData;
  }
  async parseBody(options) {
    return parseBody(this, options);
  }
  #cachedBody = (key) => {
    const { bodyCache, raw: raw2 } = this;
    const cachedBody = bodyCache[key];
    if (cachedBody) return cachedBody;
    for (const anyCachedKey in bodyCache) return bodyCache[anyCachedKey].then((body) => {
      if (anyCachedKey === "json") body = JSON.stringify(body);
      const contentType = anyCachedKey === "formData" ? void 0 : raw2.headers.get("content-type");
      return new Response(body, { headers: contentType ? { "Content-Type": contentType } : void 0 })[key]();
    });
    return bodyCache[key] = raw2[key]();
  };
  /**
  * `.json()` can parse Request body of type `application/json`
  *
  * @see {@link https://hono.dev/docs/api/request#json}
  *
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.json()
  * })
  * ```
  */
  json() {
    return this.#cachedBody("text").then((text) => JSON.parse(text));
  }
  /**
  * `.text()` can parse Request body of type `text/plain`
  *
  * @see {@link https://hono.dev/docs/api/request#text}
  *
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.text()
  * })
  * ```
  */
  text() {
    return this.#cachedBody("text");
  }
  /**
  * `.arrayBuffer()` parse Request body as an `ArrayBuffer`
  *
  * @see {@link https://hono.dev/docs/api/request#arraybuffer}
  *
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.arrayBuffer()
  * })
  * ```
  */
  arrayBuffer() {
    return this.#cachedBody("arrayBuffer");
  }
  /**
  * `.bytes()` parses the request body as a `Uint8Array`.
  *
  * @see {@link https://hono.dev/docs/api/request#bytes}
  *
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.bytes()
  * })
  * ```
  */
  bytes() {
    return this.#cachedBody("arrayBuffer").then((buffer) => new Uint8Array(buffer));
  }
  /**
  * Parses the request body as a `Blob`.
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.blob();
  * });
  * ```
  * @see https://hono.dev/docs/api/request#blob
  */
  blob() {
    return this.#cachedBody("blob");
  }
  /**
  * Parses the request body as `FormData`.
  * @example
  * ```ts
  * app.post('/entry', async (c) => {
  *   const body = await c.req.formData();
  * });
  * ```
  * @see https://hono.dev/docs/api/request#formdata
  */
  formData() {
    return this.#cachedBody("formData");
  }
  /**
  * Adds validated data to the request.
  *
  * @param target - The target of the validation.
  * @param data - The validated data to add.
  */
  addValidatedData(target, data) {
    (this.#validatedData ??= {})[target] = data;
  }
  valid(target) {
    return this.#validatedData?.[target];
  }
  /**
  * `.url` can get the request url strings.
  *
  * @see {@link https://hono.dev/docs/api/request#url}
  *
  * @example
  * ```ts
  * app.get('/about/me', (c) => {
  *   const url = c.req.url // `http://localhost:8787/about/me`
  *   ...
  * })
  * ```
  */
  get url() {
    return this.raw.url;
  }
  /**
  * `.method` can get the method name of the request.
  *
  * @see {@link https://hono.dev/docs/api/request#method}
  *
  * @example
  * ```ts
  * app.get('/about/me', (c) => {
  *   const method = c.req.method // `GET`
  * })
  * ```
  */
  get method() {
    return this.raw.method;
  }
  get [GET_MATCH_RESULT]() {
    return this.#matchResult;
  }
  /**
  * `.matchedRoutes` can return a matched route in the handler
  *
  * @deprecated
  *
  * Use matchedRoutes helper defined in "hono/route" instead.
  *
  * @see {@link https://hono.dev/docs/api/request#matchedroutes}
  *
  * @example
  * ```ts
  * app.use('*', async function logger(c, next) {
  *   await next()
  *   c.req.matchedRoutes.forEach(({ handler, method, path }, i) => {
  *     const name = handler.name || (handler.length < 2 ? '[handler]' : '[middleware]')
  *     console.log(
  *       method,
  *       ' ',
  *       path,
  *       ' '.repeat(Math.max(10 - path.length, 0)),
  *       name,
  *       i === c.req.routeIndex ? '<- respond from here' : ''
  *     )
  *   })
  * })
  * ```
  */
  get matchedRoutes() {
    return this.#matchResult[0].map(([[, route]]) => route);
  }
  /**
  * `.routePath` can retrieve the path registered within the handler
  *
  * @deprecated
  *
  * Use routePath helper defined in "hono/route" instead.
  *
  * @see {@link https://hono.dev/docs/api/request#routepath}
  *
  * @example
  * ```ts
  * app.get('/posts/:id', (c) => {
  *   return c.json({ path: c.req.routePath })
  * })
  * ```
  */
  get routePath() {
    return this.#matchResult[0].map(([[, route]]) => route)[this.routeIndex].path;
  }
};

// node_modules/hono/dist/utils/html.js
var HtmlEscapedCallbackPhase = {
  Stringify: 1,
  BeforeStream: 2,
  Stream: 3
};
var raw = (value, callbacks) => {
  const escapedString = new String(value);
  escapedString.isEscaped = true;
  escapedString.callbacks = callbacks;
  return escapedString;
};
var resolveCallback = async (str, phase, preserveCallbacks, context, buffer) => {
  if (typeof str === "object" && !(str instanceof String)) {
    if (!(str instanceof Promise)) str = str.toString();
    if (str instanceof Promise) str = await str;
  }
  const callbacks = str.callbacks;
  if (!callbacks?.length) return Promise.resolve(str);
  if (buffer) buffer[0] += str;
  else buffer = [str];
  const resStr = Promise.all(callbacks.map((c) => c({
    phase,
    buffer,
    context
  }))).then((res) => Promise.all(res.filter(Boolean).map((str2) => resolveCallback(str2, phase, false, context, buffer))).then(() => buffer[0]));
  if (preserveCallbacks) return raw(await resStr, callbacks);
  else return resStr;
};

// node_modules/hono/dist/context.js
var TEXT_PLAIN = "text/plain; charset=UTF-8";
var setDefaultContentType = (contentType, headers) => {
  return {
    "Content-Type": contentType,
    ...headers
  };
};
var createResponseInstance = (body, init) => new Response(body, init);
var Context = class {
  #rawRequest;
  #req;
  /**
  * `.env` can get bindings (environment variables, secrets, KV namespaces, D1 database, R2 bucket etc.) in Cloudflare Workers.
  *
  * @see {@link https://hono.dev/docs/api/context#env}
  *
  * @example
  * ```ts
  * // Environment object for Cloudflare Workers
  * app.get('*', async c => {
  *   const counter = c.env.COUNTER
  * })
  * ```
  */
  env = {};
  #var;
  finalized = false;
  /**
  * `.error` can get the error object from the middleware if the Handler throws an error.
  *
  * @see {@link https://hono.dev/docs/api/context#error}
  *
  * @example
  * ```ts
  * app.use('*', async (c, next) => {
  *   await next()
  *   if (c.error) {
  *     // do something...
  *   }
  * })
  * ```
  */
  error;
  #status;
  #executionCtx;
  #res;
  #layout;
  #renderer;
  #notFoundHandler;
  #preparedHeaders;
  #matchResult;
  #path;
  /**
  * Creates an instance of the Context class.
  *
  * @param req - The Request object.
  * @param options - Optional configuration options for the context.
  */
  constructor(req, options) {
    this.#rawRequest = req;
    if (options) {
      this.#executionCtx = options.executionCtx;
      this.env = options.env;
      this.#notFoundHandler = options.notFoundHandler;
      this.#path = options.path;
      this.#matchResult = options.matchResult;
    }
  }
  /**
  * `.req` is the instance of {@link HonoRequest}.
  */
  get req() {
    this.#req ??= new HonoRequest(this.#rawRequest, this.#path, this.#matchResult);
    return this.#req;
  }
  /**
  * @see {@link https://hono.dev/docs/api/context#event}
  * The FetchEvent associated with the current request.
  *
  * @throws Will throw an error if the context does not have a FetchEvent.
  */
  get event() {
    if (this.#executionCtx && "respondWith" in this.#executionCtx) return this.#executionCtx;
    else throw Error("This context has no FetchEvent");
  }
  /**
  * @see {@link https://hono.dev/docs/api/context#executionctx}
  * The ExecutionContext associated with the current request.
  *
  * @throws Will throw an error if the context does not have an ExecutionContext.
  */
  get executionCtx() {
    if (this.#executionCtx) return this.#executionCtx;
    else throw Error("This context has no ExecutionContext");
  }
  /**
  * @see {@link https://hono.dev/docs/api/context#res}
  * The Response object for the current request.
  */
  get res() {
    return this.#res ||= createResponseInstance(null, { headers: this.#preparedHeaders ??= new Headers() });
  }
  /**
  * Sets the Response object for the current request.
  *
  * @param _res - The Response object to set.
  */
  set res(_res) {
    if (this.#res && _res) {
      _res = createResponseInstance(_res.body, _res);
      for (const [k, v] of this.#res.headers.entries()) {
        if (k === "content-type") continue;
        if (k === "set-cookie") {
          const cookies = this.#res.headers.getSetCookie();
          _res.headers.delete("set-cookie");
          for (const cookie of cookies) _res.headers.append("set-cookie", cookie);
        } else _res.headers.set(k, v);
      }
    }
    this.#res = _res;
    this.finalized = true;
  }
  /**
  * `.render()` can create a response within a layout.
  *
  * @see {@link https://hono.dev/docs/api/context#render-setrenderer}
  *
  * @example
  * ```ts
  * app.get('/', (c) => {
  *   return c.render('Hello!')
  * })
  * ```
  */
  render = (...args) => {
    this.#renderer ??= (content) => this.html(content);
    return this.#renderer(...args);
  };
  /**
  * Sets the layout for the response.
  *
  * @param layout - The layout to set.
  * @returns The layout function.
  */
  setLayout = (layout) => this.#layout = layout;
  /**
  * Gets the current layout for the response.
  *
  * @returns The current layout function.
  */
  getLayout = () => this.#layout;
  /**
  * `.setRenderer()` can set the layout in the custom middleware.
  *
  * @see {@link https://hono.dev/docs/api/context#render-setrenderer}
  *
  * @example
  * ```tsx
  * app.use('*', async (c, next) => {
  *   c.setRenderer((content) => {
  *     return c.html(
  *       <html>
  *         <body>
  *           <p>{content}</p>
  *         </body>
  *       </html>
  *     )
  *   })
  *   await next()
  * })
  * ```
  */
  setRenderer = (renderer) => {
    this.#renderer = renderer;
  };
  /**
  * `.header()` can set headers.
  *
  * @see {@link https://hono.dev/docs/api/context#header}
  *
  * @example
  * ```ts
  * app.get('/welcome', (c) => {
  *   // Set headers
  *   c.header('X-Message', 'Hello!')
  *   c.header('Content-Type', 'text/plain')
  *
  *   // Append multiple headers using the append option (e.g. Vary)
  *   c.header('Vary', 'Accept-Encoding', { append: true })
  *   c.header('Vary', 'User-Agent', { append: true })
  *
  *   return c.body('Thank you for coming')
  * })
  * ```
  */
  header = (name, value, options) => {
    if (this.finalized && !this.#res.bodyUsed) this.#res = createResponseInstance(this.#res.body, this.#res);
    const headers = this.#res ? this.#res.headers : this.#preparedHeaders ??= new Headers();
    if (value === void 0) headers.delete(name);
    else if (options?.append) headers.append(name, value);
    else headers.set(name, value);
  };
  status = (status) => {
    this.#status = status;
  };
  /**
  * `.set()` can set the value specified by the key.
  *
  * @see {@link https://hono.dev/docs/api/context#set-get}
  *
  * @example
  * ```ts
  * app.use('*', async (c, next) => {
  *   c.set('message', 'Hono is hot!!')
  *   await next()
  * })
  * ```
  */
  set = (key, value) => {
    this.#var ??= /* @__PURE__ */ new Map();
    this.#var.set(key, value);
  };
  /**
  * `.get()` can use the value specified by the key.
  *
  * @see {@link https://hono.dev/docs/api/context#set-get}
  *
  * @example
  * ```ts
  * app.get('/', (c) => {
  *   const message = c.get('message')
  *   return c.text(`The message is "${message}"`)
  * })
  * ```
  */
  get = (key) => {
    return this.#var ? this.#var.get(key) : void 0;
  };
  /**
  * `.var` can access the value of a variable.
  *
  * @see {@link https://hono.dev/docs/api/context#var}
  *
  * @example
  * ```ts
  * const result = c.var.client.oneMethod()
  * ```
  */
  get var() {
    if (!this.#var) return {};
    return Object.fromEntries(this.#var);
  }
  #newResponse(data, arg, headers) {
    let responseHeaders = this.#res ? new Headers(this.#res.headers) : this.#preparedHeaders;
    if (typeof arg === "object" && arg.headers) {
      responseHeaders ??= new Headers();
      for (const [key, value] of new Headers(arg.headers)) if (key === "set-cookie") responseHeaders.append(key, value);
      else responseHeaders.set(key, value);
    }
    if (headers) {
      if (!responseHeaders) {
        let count = 0;
        for (const k in headers) if (++count > 1 || typeof headers[k] !== "string") {
          responseHeaders = new Headers();
          break;
        }
      }
      if (responseHeaders) for (const k in headers) {
        const v = headers[k];
        if (typeof v === "string") responseHeaders.set(k, v);
        else {
          responseHeaders.delete(k);
          for (const v2 of v) responseHeaders.append(k, v2);
        }
      }
    }
    const status = typeof arg === "number" ? arg : arg?.status ?? this.#status;
    return createResponseInstance(data, {
      status,
      headers: responseHeaders ?? headers
    });
  }
  newResponse = (...args) => this.#newResponse(...args);
  /**
  * `.body()` can return the HTTP response.
  * You can set headers with `.header()` and set HTTP status code with `.status`.
  * This can also be set in `.text()`, `.json()` and so on.
  *
  * @see {@link https://hono.dev/docs/api/context#body}
  *
  * @example
  * ```ts
  * app.get('/welcome', (c) => {
  *   // Set headers
  *   c.header('X-Message', 'Hello!')
  *   c.header('Content-Type', 'text/plain')
  *   // Set HTTP status code
  *   c.status(201)
  *
  *   // Return the response body
  *   return c.body('Thank you for coming')
  * })
  * ```
  */
  body = (data, arg, headers) => this.#newResponse(data, arg, headers);
  /**
  * `.text()` can render text as `Content-Type:text/plain`.
  *
  * @see {@link https://hono.dev/docs/api/context#text}
  *
  * @example
  * ```ts
  * app.get('/say', (c) => {
  *   return c.text('Hello!')
  * })
  * ```
  */
  text = (text, arg, headers) => {
    return !this.#preparedHeaders && !this.#status && !arg && !headers && !this.finalized ? new Response(text) : this.#newResponse(text, arg, setDefaultContentType(TEXT_PLAIN, headers));
  };
  /**
  * `.json()` can render JSON as `Content-Type:application/json`.
  *
  * @see {@link https://hono.dev/docs/api/context#json}
  *
  * @example
  * ```ts
  * app.get('/api', (c) => {
  *   return c.json({ message: 'Hello!' })
  * })
  * ```
  */
  json = (object, arg, headers) => {
    return this.#newResponse(JSON.stringify(object), arg, setDefaultContentType("application/json", headers));
  };
  html = (html, arg, headers) => {
    const res = (html2) => this.#newResponse(html2, arg, setDefaultContentType("text/html; charset=UTF-8", headers));
    return typeof html === "object" ? resolveCallback(html, HtmlEscapedCallbackPhase.Stringify, false, {}).then(res) : res(html);
  };
  /**
  * `.redirect()` can Redirect, default status code is 302.
  *
  * @see {@link https://hono.dev/docs/api/context#redirect}
  *
  * @example
  * ```ts
  * app.get('/redirect', (c) => {
  *   return c.redirect('/')
  * })
  * app.get('/redirect-permanently', (c) => {
  *   return c.redirect('/', 301)
  * })
  * ```
  */
  redirect = (location, status) => {
    const locationString = String(location);
    this.header("Location", !/[^\x00-\xFF]/.test(locationString) ? locationString : encodeURI(locationString));
    return this.newResponse(null, status ?? 302);
  };
  /**
  * `.notFound()` can return the Not Found Response.
  *
  * @see {@link https://hono.dev/docs/api/context#notfound}
  *
  * @example
  * ```ts
  * app.get('/notfound', (c) => {
  *   return c.notFound()
  * })
  * ```
  */
  notFound = () => {
    this.#notFoundHandler ??= () => createResponseInstance();
    return this.#notFoundHandler(this);
  };
};

// node_modules/hono/dist/compose.js
var compose = (middleware, onError, onNotFound) => {
  return (context, next) => {
    let index = -1;
    return dispatch(0);
    async function dispatch(i) {
      if (i <= index) throw new Error("next() called multiple times");
      index = i;
      let res;
      let isError = false;
      let handler;
      if (middleware[i]) {
        handler = middleware[i][0][0];
        context.req.routeIndex = i;
      } else handler = i === middleware.length && next || void 0;
      if (handler) try {
        res = await handler(context, () => dispatch(i + 1));
      } catch (err) {
        if (err instanceof Error && onError) {
          context.error = err;
          res = await onError(err, context);
          isError = true;
        } else throw err;
      }
      else if (context.finalized === false && onNotFound) res = await onNotFound(context);
      if (res && (context.finalized === false || isError)) context.res = res;
      return context;
    }
  };
};

// node_modules/hono/dist/router.js
var METHODS = [
  "get",
  "post",
  "put",
  "delete",
  "options",
  "patch",
  "query"
];
var MESSAGE_MATCHER_IS_ALREADY_BUILT = "Can not add a route since the matcher is already built.";
var UnsupportedPathError = class extends Error {
};

// node_modules/hono/dist/utils/constants.js
var COMPOSED_HANDLER = "__COMPOSED_HANDLER";

// node_modules/hono/dist/utils/mount.js
var getPathPrefixLength = (c, pathPrefix, routePath) => {
  if (!/[:*]/.test(pathPrefix)) return pathPrefix.length;
  const path = c.req.path;
  const matchResult = c.req[GET_MATCH_RESULT];
  const paramNames = /* @__PURE__ */ new Set();
  let length = 0;
  for (const part of splitRoutingPath(pathPrefix)) {
    const pattern = getPattern(part);
    if (pattern && pattern !== "*") {
      const name = pattern[1];
      if (paramNames.has(name)) throw new Error(`mount(): Duplicate path parameter "${name}" in "${routePath}".`);
      paramNames.add(name);
    }
    if (pattern && pattern !== "*" && pattern[2] !== true) {
      const value = matchResult.length === 2 ? matchResult[1][matchResult[0][c.req.routeIndex][1][pattern[1]]] : matchResult[0][c.req.routeIndex][1][pattern[1]];
      length += value.length + 1;
    } else if (pattern || part.endsWith("*")) {
      const end = path.indexOf("/", length + 1);
      length = end === -1 ? path.length : end;
    } else length += part.length + 1;
  }
  return length;
};
var createMountedRequest = (c, pathPrefix, routePath) => {
  if (/[^/]\*$/.test(routePath)) throw new Error(`mount(): Wildcard must follow "/" in "${routePath}".`);
  const url = new URL(c.req.raw.url);
  url.pathname = stripPathPrefix(url.pathname, c.req.path, getPathPrefixLength(c, pathPrefix, routePath));
  return new Request(url, c.req.raw);
};

// node_modules/hono/dist/hono-base.js
var notFoundHandler = (c) => {
  return c.text("404 Not Found", 404);
};
var errorHandler = (err, c) => {
  if ("getResponse" in err) {
    const res = err.getResponse();
    return c.newResponse(res.body, res);
  }
  console.error(err);
  return c.text("Internal Server Error", 500);
};
var Hono = class Hono2 {
  get;
  post;
  put;
  delete;
  options;
  patch;
  query;
  all;
  on;
  use;
  router;
  getPath;
  _basePath = "/";
  #path = "/";
  routes = [];
  constructor(options = {}) {
    [...METHODS, "all"].forEach((method) => {
      this[method] = (args1, ...args) => {
        const methodName = method.toUpperCase();
        if (typeof args1 === "string") this.#path = args1;
        else this.#addRoute(methodName, this.#path, args1);
        args.forEach((handler) => {
          this.#addRoute(methodName, this.#path, handler);
        });
        return this;
      };
    });
    this.on = (method, path, ...handlers) => {
      for (const p of [path].flat()) {
        this.#path = p;
        for (const m of [method].flat()) {
          const methodName = m.toUpperCase();
          for (const handler of handlers) this.#addRoute(methodName, this.#path, handler);
        }
      }
      return this;
    };
    this.use = (arg1, ...handlers) => {
      if (typeof arg1 === "string") this.#path = arg1;
      else {
        this.#path = "*";
        handlers.unshift(arg1);
      }
      handlers.forEach((handler) => {
        this.#addRoute("ALL", this.#path, handler);
      });
      return this;
    };
    const { strict, ...optionsWithoutStrict } = options;
    Object.assign(this, optionsWithoutStrict);
    this.getPath = strict ?? true ? options.getPath ?? getPath : getPathNoStrict;
  }
  #clone() {
    const clone = new Hono2({
      router: this.router,
      getPath: this.getPath
    });
    clone.errorHandler = this.errorHandler;
    clone.#notFoundHandler = this.#notFoundHandler;
    clone.routes = this.routes;
    return clone;
  }
  #notFoundHandler = notFoundHandler;
  errorHandler = errorHandler;
  /**
  * `.route()` allows grouping other Hono instance in routes.
  *
  * @see {@link https://hono.dev/docs/api/routing#grouping}
  *
  * @param {string} path - base Path
  * @param {Hono} app - other Hono instance
  * @returns {Hono} routed Hono instance
  *
  * @example
  * ```ts
  * const app = new Hono()
  * const app2 = new Hono()
  *
  * app2.get("/user", (c) => c.text("user"))
  * app.route("/api", app2) // GET /api/user
  * ```
  */
  route(path, app2) {
    const subApp = this.basePath(path);
    app2.routes.map((r) => {
      let handler;
      if (app2.errorHandler === errorHandler) handler = r.handler;
      else {
        handler = async (c, next) => (await compose([], app2.errorHandler)(c, () => r.handler(c, next))).res;
        handler[COMPOSED_HANDLER] = r.handler;
      }
      subApp.#addRoute(r.method, r.path, handler, r.basePath);
    });
    return this;
  }
  /**
  * `.basePath()` allows base paths to be specified.
  *
  * @see {@link https://hono.dev/docs/api/routing#base-path}
  *
  * @param {string} path - base Path
  * @returns {Hono} changed Hono instance
  *
  * @example
  * ```ts
  * const api = new Hono().basePath('/api')
  * ```
  */
  basePath(path) {
    const subApp = this.#clone();
    subApp._basePath = mergePath(this._basePath, path);
    return subApp;
  }
  /**
  * `.onError()` handles an error and returns a customized Response.
  *
  * @see {@link https://hono.dev/docs/api/hono#error-handling}
  *
  * @param {ErrorHandler} handler - request Handler for error
  * @returns {Hono} changed Hono instance
  *
  * @example
  * ```ts
  * app.onError((err, c) => {
  *   console.error(`${err}`)
  *   return c.text('Custom Error Message', 500)
  * })
  * ```
  */
  onError = (handler) => {
    this.errorHandler = handler;
    return this;
  };
  /**
  * `.notFound()` allows you to customize a Not Found Response.
  *
  * @see {@link https://hono.dev/docs/api/hono#not-found}
  *
  * @param {NotFoundHandler} handler - request handler for not-found
  * @returns {Hono} changed Hono instance
  *
  * @example
  * ```ts
  * app.notFound((c) => {
  *   return c.text('Custom 404 Message', 404)
  * })
  * ```
  */
  notFound = (handler) => {
    this.#notFoundHandler = handler;
    return this;
  };
  /**
  * `.mount()` allows you to mount applications built with other frameworks into your Hono application.
  *
  * @deprecated Use `mount()` from `hono/mount` instead. `.mount()` will be removed in v5.
  *
  * @see {@link https://hono.dev/docs/api/hono#mount}
  *
  * @param {string} path - base Path
  * @param {Function} applicationHandler - other Request Handler
  * @param {MountOptions} [options] - options of `.mount()`
  * @returns {Hono} mounted Hono instance
  *
  * @example
  * ```ts
  * import { Router as IttyRouter } from 'itty-router'
  * import { Hono } from 'hono'
  * // Create itty-router application
  * const ittyRouter = IttyRouter()
  * // GET /itty-router/hello
  * ittyRouter.get('/hello', () => new Response('Hello from itty-router'))
  *
  * const app = new Hono()
  * app.mount('/itty-router', ittyRouter.handle)
  * ```
  *
  * @example
  * ```ts
  * const app = new Hono()
  * // Send the request to another application without modification.
  * app.mount('/app', anotherApp, {
  *   replaceRequest: (req) => req,
  * })
  * ```
  */
  mount(path, applicationHandler, options) {
    let replaceRequest;
    let optionHandler;
    if (options) {
      if (typeof options === "function") optionHandler = options;
      else {
        optionHandler = options.optionHandler;
        if (options.replaceRequest === false) replaceRequest = (request) => request;
        else replaceRequest = options.replaceRequest;
      }
    }
    const getOptions = optionHandler ? (c) => {
      const options2 = optionHandler(c);
      return Array.isArray(options2) ? options2 : [options2];
    } : (c) => {
      let executionContext = void 0;
      try {
        executionContext = c.executionCtx;
      } catch {
      }
      return [c.env, executionContext];
    };
    const mergedPath = mergePath(this._basePath, path);
    const defaultReplaceRequest = (c) => createMountedRequest(c, mergedPath.replace(/\/$/, ""), mergePath(mergedPath, "*"));
    const handler = async (c, next) => {
      const res = await applicationHandler(replaceRequest ? replaceRequest(c.req.raw) : defaultReplaceRequest(c), ...getOptions(c));
      if (res) return res;
      await next();
    };
    this.#addRoute("ALL", mergePath(path, "*"), handler);
    return this;
  }
  #addRoute(method, path, handler, baseRoutePath) {
    path = mergePath(this._basePath, path);
    const r = {
      basePath: baseRoutePath !== void 0 ? mergePath(this._basePath, baseRoutePath) : this._basePath,
      path,
      method,
      handler
    };
    this.router.add(method, path, [handler, r]);
    this.routes.push(r);
  }
  #handleError(err, c) {
    if (err instanceof Error) return this.errorHandler(err, c);
    throw err;
  }
  #dispatch(request, executionCtx, env, method) {
    if (method === "HEAD") return (async () => new Response(null, await this.#dispatch(request, executionCtx, env, "GET")))();
    const path = this.getPath(request, { env });
    const matchResult = this.router.match(method, path);
    const c = new Context(request, {
      path,
      matchResult,
      env,
      executionCtx,
      notFoundHandler: this.#notFoundHandler
    });
    if (matchResult[0].length === 1) {
      let res;
      try {
        res = matchResult[0][0][0][0](c, async () => {
          c.res = await this.#notFoundHandler(c);
        });
      } catch (err) {
        return this.#handleError(err, c);
      }
      return res instanceof Promise ? res.then((resolved) => resolved || (c.finalized ? c.res : this.#notFoundHandler(c))).catch((err) => this.#handleError(err, c)) : res ?? this.#notFoundHandler(c);
    }
    const composed = compose(matchResult[0], this.errorHandler, this.#notFoundHandler);
    return (async () => {
      try {
        const context = await composed(c);
        if (!context.finalized) throw new Error("Context is not finalized. Did you forget to return a Response object or `await next()`?");
        return context.res;
      } catch (err) {
        return this.#handleError(err, c);
      }
    })();
  }
  /**
  * `.fetch()` will be entry point of your app.
  *
  * @see {@link https://hono.dev/docs/api/hono#fetch}
  *
  * @param {Request} request - request Object of request
  * @param {Env} env - env Object
  * @param {ExecutionContext} executionCtx - context of execution
  * @returns {Response | Promise<Response>} response of request
  *
  */
  fetch = (request, ...rest) => {
    return this.#dispatch(request, rest[1], rest[0], request.method);
  };
  /**
  * `.request()` is a useful method for testing.
  * You can pass a URL or pathname to send a GET request.
  * app will return a Response object.
  * ```ts
  * test('GET /hello is ok', async () => {
  *   const res = await app.request('/hello')
  *   expect(res.status).toBe(200)
  * })
  * ```
  * @see https://hono.dev/docs/api/hono#request
  */
  request = (input, requestInit, Env, executionCtx) => {
    if (input instanceof Request) return this.fetch(requestInit ? new Request(input, requestInit) : input, Env, executionCtx);
    input = input.toString();
    return this.fetch(new Request(/^https?:\/\//.test(input) ? input : `http://localhost${mergePath("/", input)}`, requestInit), Env, executionCtx);
  };
  /**
  * `.fire()` automatically adds a global fetch event listener.
  * This can be useful for environments that adhere to the Service Worker API, such as non-ES module Cloudflare Workers.
  * @deprecated
  * Use `fire` from `hono/service-worker` instead.
  * ```ts
  * import { Hono } from 'hono'
  * import { fire } from 'hono/service-worker'
  *
  * const app = new Hono()
  * // ...
  * fire(app)
  * ```
  * @see https://hono.dev/docs/api/hono#fire
  * @see https://developer.mozilla.org/en-US/docs/Web/API/Service_Worker_API
  * @see https://developers.cloudflare.com/workers/reference/migrate-to-module-workers/
  */
  fire = () => {
    addEventListener("fetch", (event) => {
      event.respondWith(this.#dispatch(event.request, event, void 0, event.request.method));
    });
  };
};

// node_modules/hono/dist/router/utils.js
var createNullObject = () => /* @__PURE__ */ Object.create(null);

// node_modules/hono/dist/router/reg-exp-router/matcher.js
var emptyParam = [];
function match(method, path) {
  const matchers = this.buildAllMatchers();
  const match2 = ((method2, path2) => {
    const matcher = matchers[method2] || matchers["ALL"];
    const staticMatch = matcher[2][path2];
    if (staticMatch) return staticMatch;
    const match3 = path2.match(matcher[0]);
    if (!match3) return [[], emptyParam];
    const index = match3.indexOf("", 1);
    return [matcher[1][index], match3];
  });
  this.match = match2;
  return match2(method, path);
}

// node_modules/hono/dist/router/reg-exp-router/node.js
var LABEL_REG_EXP_STR = "[^/]+";
var ONLY_WILDCARD_REG_EXP_STR = "[^]*";
var TAIL_WILDCARD_REG_EXP_STR = "(?:|/[^]*)";
var PATH_ERROR = Symbol();
var regExpMetaChars = /* @__PURE__ */ new Set(".\\+*[^]$()");
function compareKey(a, b) {
  if (a.length === 1) return b.length === 1 ? a < b ? -1 : 1 : -1;
  if (b.length === 1) return 1;
  if (a === "[^]*" || a === "(?:|/[^]*)") return b === "[^]*" ? -1 : 1;
  else if (b === "[^]*" || b === "(?:|/[^]*)") return -1;
  if (a === "[^/]+") return 1;
  else if (b === "[^/]+") return -1;
  return a.length === b.length ? a < b ? -1 : 1 : b.length - a.length;
}
var Node = class Node2 {
  #index;
  #varIndex;
  #children = createNullObject();
  insert(tokens, index, paramMap, context, keys) {
    let node = this;
    for (let i = 0, len = tokens.length; i < len; i++) {
      const token = tokens[i];
      const pattern = token.length === 1 ? token === "*" ? i === len - 1 ? [
        "",
        "",
        ONLY_WILDCARD_REG_EXP_STR
      ] : [
        "",
        "",
        LABEL_REG_EXP_STR
      ] : null : token === "/*" ? [
        "",
        "",
        TAIL_WILDCARD_REG_EXP_STR
      ] : token.match(/^\:([^\{\}]+)(?:\{(.+)\})?$/);
      let nextNode;
      if (pattern) {
        const name = pattern[1];
        let regexpStr = pattern[2] || "[^/]+";
        if (name && pattern[2]) {
          if (new RegExp(regexpStr).test("")) throw PATH_ERROR;
          regexpStr = regexpStr.replace(/^\((?!\?:)(?=[^)]+\)$)/, "(?:");
          if (/\((?!\?:)/.test(regexpStr)) throw PATH_ERROR;
          if (regexpStr.length === 1 && regExpMetaChars.has(regexpStr)) throw PATH_ERROR;
        }
        keys.push(regexpStr);
        nextNode = node.#children[regexpStr];
        if (!nextNode) {
          if (regexpStr !== "[^]*" && regexpStr !== "(?:|/[^]*)") {
            for (const k in node.#children) if ((regexpStr.length > 1 || k.length > 1) && k !== "[^]*" && k !== "(?:|/[^]*)") throw PATH_ERROR;
          }
          nextNode = node.#children[regexpStr] = new Node2();
        }
        if (name !== "") {
          nextNode.#varIndex ??= context.varIndex++;
          paramMap.push([name, nextNode.#varIndex]);
        }
      } else {
        keys.push(token);
        nextNode = node.#children[token];
        if (!nextNode) {
          for (const k in node.#children) if (k.length > 1 && k !== "[^]*" && k !== "(?:|/[^]*)") throw PATH_ERROR;
          nextNode = node.#children[token] = new Node2();
        }
      }
      node = nextNode;
    }
    if (node.#index !== void 0) throw PATH_ERROR;
    node.#index = index;
  }
  buildRegExpStr() {
    const strList = Object.keys(this.#children).sort(compareKey).map((k) => {
      const c = this.#children[k];
      const childStr = c.buildRegExpStr();
      return childStr === "" ? "" : (typeof c.#varIndex === "number" ? `(${k})@${c.#varIndex}` : regExpMetaChars.has(k) ? `\\${k}` : k) + childStr;
    }).filter(Boolean);
    if (typeof this.#index === "number" && this.#index !== -1) strList.unshift(`#${this.#index}`);
    if (strList.length === 0) return "";
    if (strList.length === 1) return strList[0];
    return "(?:" + strList.join("|") + ")";
  }
};

// node_modules/hono/dist/router/reg-exp-router/trie.js
var Trie = class {
  #context = { varIndex: 0 };
  #root = new Node();
  #index = 0;
  paths = createNullObject();
  insert(path, isStatic) {
    const keys = [];
    if (isStatic) {
      this.#root.insert(path.split(""), -1, [], this.#context, keys);
      this.paths[path] = [
        -1,
        [],
        keys
      ];
      return;
    }
    const paramAssoc = [];
    const groups = [];
    let markedPath = path;
    for (let i = 0; ; ) {
      let replaced = false;
      markedPath = markedPath.replace(/\{(?:\{[\d,]+\}|[^}])+\}/g, (m) => {
        const mark = `@\\${i}`;
        groups[i] = [mark, m];
        i++;
        replaced = true;
        return mark;
      });
      if (!replaced) break;
    }
    const tokens = markedPath.match(/(?::[^\/]+)|(?:\/\*$)|./gs) || [];
    for (let i = groups.length - 1; i >= 0; i--) {
      const [mark] = groups[i];
      for (let j = tokens.length - 1; j >= 0; j--) if (tokens[j].indexOf(mark) !== -1) {
        tokens[j] = tokens[j].replace(mark, groups[i][1]);
        break;
      }
    }
    this.#root.insert(tokens, this.#index, paramAssoc, this.#context, keys);
    this.paths[path] = [
      this.#index++,
      paramAssoc,
      keys
    ];
  }
  buildRegExp() {
    let regexp = this.#root.buildRegExpStr();
    if (regexp === "") return [
      /^$/,
      [],
      []
    ];
    let captureIndex = 0;
    const indexReplacementMap = [];
    const paramReplacementMap = [];
    regexp = regexp.replace(/#(\d+)|@(\d+)|\.\*\$/g, (_, handlerIndex, paramIndex) => {
      if (handlerIndex !== void 0) {
        indexReplacementMap[++captureIndex] = Number(handlerIndex);
        return "$()";
      }
      if (paramIndex !== void 0) {
        paramReplacementMap[Number(paramIndex)] = ++captureIndex;
        return "";
      }
      return "";
    });
    return [
      new RegExp(`^${regexp}`),
      indexReplacementMap,
      paramReplacementMap
    ];
  }
};

// node_modules/hono/dist/router/reg-exp-router/router.js
function endsWithWildcard(keys) {
  const tail = keys[keys.length - 1];
  return tail === "[^]*" || tail === "(?:|/[^]*)";
}
function covers(wildcard, target) {
  const last = wildcard.length - 1;
  for (let i = 0; i < last; i++) if (wildcard[i] !== target[i]) return false;
  return wildcard[last] === "[^]*" || target.length === last || target[last] === "/";
}
var RegExpRouter = class {
  name = "RegExpRouter";
  #handlers;
  #tries;
  constructor() {
    this.#handlers = { ["ALL"]: [] };
    this.#tries = { ["ALL"]: new Trie() };
  }
  #insertPath(method, path) {
    try {
      this.#tries[method].insert(path, !/\*|\/:/.test(path));
    } catch (e) {
      throw e === PATH_ERROR ? new UnsupportedPathError(path) : e;
    }
  }
  add(method, path, handler) {
    const handlers = this.#handlers;
    if (!handlers) throw new Error(MESSAGE_MATCHER_IS_ALREADY_BUILT);
    const tries = this.#tries;
    if (!handlers[method]) {
      tries[method] = new Trie();
      handlers[method] = [...handlers["ALL"]];
      for (const p in tries["ALL"].paths) this.#insertPath(method, p);
    }
    if (path === "/*") path = "*";
    const methods = method === "ALL" ? Object.keys(handlers) : [method];
    const paths = checkOptionalParameter(path) || [path];
    for (const path2 of paths) for (const m of methods) {
      if (!tries[m].paths[path2]) this.#insertPath(m, path2);
      handlers[m].push([handler, path2]);
    }
  }
  match = match;
  buildAllMatchers() {
    const matchers = createNullObject();
    for (const method of Object.keys(this.#handlers)) matchers[method] = this.#buildMatcher(method);
    this.#handlers = this.#tries = void 0;
    return matchers;
  }
  #buildMatcher(method) {
    const handlers = this.#handlers[method];
    const trie = this.#tries[method];
    const staticMap = createNullObject();
    const handlerData = [];
    const [regexp, indexReplacementMap, paramReplacementMap] = trie.buildRegExp();
    const own = createNullObject();
    const entries = handlers.map(([h, p], i) => {
      (own[p] ??= []).push(i);
      return [h, trie.paths[p][1].reduceRight((map, [key, varIndex]) => {
        map[key] = paramReplacementMap[varIndex];
        return map;
      }, createNullObject())];
    });
    const wildcards = Object.keys(own).filter((p) => endsWithWildcard(trie.paths[p][2]));
    for (const path in trie.paths) {
      const [index, , keys] = trie.paths[path];
      const indexes = [...own[path]];
      for (const p of wildcards) if (p !== path && covers(trie.paths[p][2], keys)) indexes.push(...own[p]);
      const data = indexes.sort((a, b) => a - b).map((i) => entries[i]);
      if (index === -1) {
        const last = Math.max(-1, ...data.flatMap(([, map]) => Object.values(map)));
        staticMap[path] = [data, last === -1 ? emptyParam : path.match(regexp).slice(0, last + 1)];
      } else handlerData[index] = data;
    }
    return [
      regexp,
      indexReplacementMap.map((i) => handlerData[i]),
      staticMap
    ];
  }
};

// node_modules/hono/dist/router/smart-router/router.js
var SmartRouter = class {
  name = "SmartRouter";
  #routers = [];
  #routes = [];
  constructor(init) {
    this.#routers = init.routers;
  }
  add(method, path, handler) {
    if (!this.#routes) throw new Error(MESSAGE_MATCHER_IS_ALREADY_BUILT);
    this.#routes.push([
      method,
      path,
      handler
    ]);
  }
  match(method, path) {
    if (!this.#routes) throw new Error("Fatal error");
    const routers = this.#routers;
    const routes = this.#routes;
    const len = routers.length;
    let i = 0;
    let res;
    for (; i < len; i++) {
      const router = routers[i];
      try {
        for (let i2 = 0, len2 = routes.length; i2 < len2; i2++) router.add(...routes[i2]);
        res = router.match(method, path);
      } catch (e) {
        if (e instanceof UnsupportedPathError) continue;
        throw e;
      }
      this.match = router.match.bind(router);
      this.#routers = [router];
      this.#routes = void 0;
      break;
    }
    if (i === len) throw new Error("Fatal error");
    this.name = `SmartRouter + ${this.activeRouter.name}`;
    return res;
  }
  get activeRouter() {
    if (this.#routes || this.#routers.length !== 1) throw new Error("No active router has been determined yet.");
    return this.#routers[0];
  }
};

// node_modules/hono/dist/router/trie-router/node.js
var emptyParams = createNullObject();
var order = 0;
var Node3 = class Node4 {
  #methods = [];
  #children = createNullObject();
  #patterns = [];
  #pattern;
  #params = emptyParams;
  insert(method, path, handler) {
    let curNode = this;
    const parts = splitRoutingPath(path);
    const possibleKeys = /* @__PURE__ */ new Set();
    let i = 0;
    for (const p of parts) {
      const nextP = parts[++i];
      const pattern = getPattern(p, nextP) || (nextP === void 0 && p && p.indexOf("*") === p.length - 1 ? p : null);
      const isParam = Array.isArray(pattern);
      const key = isParam ? pattern[0] : pattern || p;
      const child = curNode.#children[key] ||= new Node4();
      if (pattern && !child.#pattern) {
        child.#pattern = pattern;
        curNode.#patterns.push(child);
      }
      curNode = child;
      if (isParam) possibleKeys.add(pattern[1]);
    }
    curNode.#methods.push({ [method]: {
      handler,
      possibleKeys: [...possibleKeys],
      score: ++order
    } });
  }
  #pushHandlerSets(handlerSets, node, method, nodeParams, params) {
    for (let i = 0, len = node.#methods.length; i < len; i++) {
      const m = node.#methods[i];
      const handlerSet = m[method] || m["ALL"];
      if (handlerSet) {
        handlerSet.params = createNullObject();
        handlerSets.push(handlerSet);
        for (let i2 = 0, len2 = handlerSet.possibleKeys.length; i2 < len2; i2++) {
          const key = handlerSet.possibleKeys[i2];
          handlerSet.params[key] = params?.[key] && !i2 ? params[key] : nodeParams[key] ?? params?.[key];
        }
      }
    }
  }
  search(method, path) {
    const handlerSets = [];
    this.#params = emptyParams;
    let curNodes = [this];
    const parts = splitPath(path);
    const curNodesQueue = [];
    const len = parts.length;
    let partOffsets = null;
    for (let i = 0; i < len; i++) {
      const part = parts[i];
      const isLast = i === len - 1;
      const tempNodes = [];
      for (let j = 0, len2 = curNodes.length; j < len2; j++) {
        const node = curNodes[j];
        const nextNode = node.#children[part];
        if (nextNode) {
          nextNode.#params = node.#params;
          if (isLast) {
            if (nextNode.#children["*"]) this.#pushHandlerSets(handlerSets, nextNode.#children["*"], method, node.#params);
            this.#pushHandlerSets(handlerSets, nextNode, method, node.#params);
          } else tempNodes.push(nextNode);
        }
        for (const child of node.#patterns) {
          const pattern = child.#pattern;
          const params = node.#params === emptyParams ? {} : { ...node.#params };
          if (typeof pattern === "string") {
            if (pattern === "*" || part.startsWith(pattern.slice(0, -1))) {
              this.#pushHandlerSets(handlerSets, child, method, node.#params);
              if (pattern === "*") {
                child.#params = params;
                tempNodes.push(child);
              }
            }
            continue;
          }
          const [, name, matcher] = pattern;
          if (!part && matcher === true) continue;
          if (matcher !== true) {
            if (!partOffsets) {
              partOffsets = [];
              let offset = path[0] === "/" ? 1 : 0;
              for (let p = 0; p < len; p++) {
                partOffsets[p] = offset;
                offset += parts[p].length + 1;
              }
            }
            const restPathString = path.slice(partOffsets[i]);
            const m = matcher.exec(restPathString);
            if (m) {
              if (!Object.hasOwn(params, name)) params[name] = m[0];
              this.#pushHandlerSets(handlerSets, child, method, node.#params, params);
              if (m[0].length === restPathString.length && child.#children["*"]) this.#pushHandlerSets(handlerSets, child.#children["*"], method, node.#params, params);
              for (const _ in child.#children) {
                child.#params = params;
                const componentCount = m[0].match(/\//g)?.length ?? 0;
                (curNodesQueue[componentCount] ||= []).push(child);
                break;
              }
              continue;
            }
          }
          if (matcher === true || matcher.test(part)) {
            if (!Object.hasOwn(params, name)) params[name] = part;
            if (isLast) {
              this.#pushHandlerSets(handlerSets, child, method, params, node.#params);
              if (child.#children["*"]) this.#pushHandlerSets(handlerSets, child.#children["*"], method, params, node.#params);
            } else {
              child.#params = params;
              tempNodes.push(child);
            }
          }
        }
      }
      const shifted = curNodesQueue.shift();
      curNodes = shifted ? tempNodes.concat(shifted) : tempNodes;
    }
    if (handlerSets[1]) handlerSets.sort((a, b) => {
      return a.score - b.score;
    });
    return [handlerSets.map(({ handler, params }) => [handler, params])];
  }
};

// node_modules/hono/dist/router/trie-router/router.js
var TrieRouter = class {
  name = "TrieRouter";
  #node = new Node3();
  add(method, path, handler) {
    for (const result of checkOptionalParameter(path) || [path]) this.#node.insert(method, result, handler);
  }
  match(method, path) {
    return this.#node.search(method, path);
  }
};

// node_modules/hono/dist/hono.js
var Hono3 = class extends Hono {
  /**
  * Creates an instance of the Hono class.
  *
  * @param options - Optional configuration options for the Hono instance.
  */
  constructor(options = {}) {
    super(options);
    this.router = options.router ?? new SmartRouter({ routers: [new RegExpRouter(), new TrieRouter()] });
  }
};

// node_modules/hono/dist/adapter/cloudflare-pages/handler.js
var handle = (app2) => (eventContext) => {
  return app2.fetch(eventContext.request, {
    ...eventContext.env,
    eventContext
  }, {
    waitUntil: eventContext.waitUntil,
    passThroughOnException: eventContext.passThroughOnException,
    props: {}
  });
};

// node_modules/jose/dist/webapi/lib/buffer_utils.js
var encoder = new TextEncoder();
var decoder = new TextDecoder();
var strictDecoder = new TextDecoder("utf-8", { fatal: true });
var UINT32_RANGE = 2 ** 32;
function concat(...buffers) {
  const size = buffers.reduce((acc, { length }) => acc + length, 0), buf = new Uint8Array(size);
  let i = 0;
  for (const buffer of buffers)
    buf.set(buffer, i), i += buffer.length;
  return buf;
}
var NON_ASCII = /[^\x00-\x7f]/;
function encode(string) {
  if (typeof string == "string" && string.length >= 128) {
    if (NON_ASCII.test(string))
      throw new TypeError("non-ASCII string encountered in encode()");
    return encoder.encode(string);
  }
  const bytes = new Uint8Array(string.length);
  for (let i = 0; i < string.length; i++) {
    const code = string.charCodeAt(i);
    if (code > 127)
      throw new TypeError("non-ASCII string encountered in encode()");
    bytes[i] = code;
  }
  return bytes;
}
function encodeBase64(input, url = false) {
  if (Uint8Array.prototype.toBase64)
    return input.toBase64({ alphabet: url ? "base64url" : "base64", omitPadding: url });
  const CHUNK_SIZE = 32768, arr = [];
  for (let i = 0; i < input.length; i += CHUNK_SIZE)
    arr.push(String.fromCharCode.apply(null, input.subarray(i, i + CHUNK_SIZE)));
  const encoded = btoa(arr.join(""));
  return url ? encoded.replace(/=/g, "").replace(/\+/g, "-").replace(/\//g, "_") : encoded;
}
function decodeBase64(encoded, url = false) {
  if (Uint8Array.fromBase64)
    return Uint8Array.fromBase64(encoded, { alphabet: url ? "base64url" : "base64" });
  if (url) {
    if (encoded.includes("+") || encoded.includes("/"))
      throw new TypeError("Invalid base64url");
    encoded = encoded.replace(/-/g, "+").replace(/_/g, "/");
  }
  const binary = atob(encoded), bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++)
    bytes[i] = binary.charCodeAt(i);
  return bytes;
}

// node_modules/jose/dist/webapi/util/errors.js
var JOSEError = class extends Error {
  static code = "ERR_JOSE_GENERIC";
  code = "ERR_JOSE_GENERIC";
  constructor(message2, options) {
    super(message2, options), this.name = this.constructor.name, Error.captureStackTrace?.(this, this.constructor);
  }
};
var JWTClaimValidationFailed = class extends JOSEError {
  static code = "ERR_JWT_CLAIM_VALIDATION_FAILED";
  code = "ERR_JWT_CLAIM_VALIDATION_FAILED";
  claim;
  reason;
  payload;
  constructor(message2, payload, claim = "unspecified", reason = "unspecified") {
    super(message2, { cause: { claim, reason, payload } }), this.claim = claim, this.reason = reason, this.payload = payload;
  }
};
var JWTExpired = class extends JOSEError {
  static code = "ERR_JWT_EXPIRED";
  code = "ERR_JWT_EXPIRED";
  claim;
  reason;
  payload;
  constructor(message2, payload, claim = "unspecified", reason = "unspecified") {
    super(message2, { cause: { claim, reason, payload } }), this.claim = claim, this.reason = reason, this.payload = payload;
  }
};
var JOSEAlgNotAllowed = class extends JOSEError {
  static code = "ERR_JOSE_ALG_NOT_ALLOWED";
  code = "ERR_JOSE_ALG_NOT_ALLOWED";
};
var JOSENotSupported = class extends JOSEError {
  static code = "ERR_JOSE_NOT_SUPPORTED";
  code = "ERR_JOSE_NOT_SUPPORTED";
};
var JWSInvalid = class extends JOSEError {
  static code = "ERR_JWS_INVALID";
  code = "ERR_JWS_INVALID";
};
var JWTInvalid = class extends JOSEError {
  static code = "ERR_JWT_INVALID";
  code = "ERR_JWT_INVALID";
};
var JWSSignatureVerificationFailed = class extends JOSEError {
  static code = "ERR_JWS_SIGNATURE_VERIFICATION_FAILED";
  code = "ERR_JWS_SIGNATURE_VERIFICATION_FAILED";
  constructor(message2 = "signature verification failed", options) {
    super(message2, options);
  }
};

// node_modules/jose/dist/webapi/util/base64url.js
var invalid = "The input to be decoded is not correctly encoded.";
function decode(input) {
  try {
    return decodeBase64(typeof input == "string" ? input : decoder.decode(input), true);
  } catch (cause) {
    throw new TypeError(invalid, { cause });
  }
}
function encode2(input) {
  return encodeBase64(typeof input == "string" ? encoder.encode(input) : input, true);
}

// node_modules/jose/dist/webapi/lib/validate.js
function isObject(input) {
  if (typeof input != "object" || input === null || Object.prototype.toString.call(input) !== "[object Object]")
    return false;
  const prototype = Object.getPrototypeOf(input);
  return prototype === null || Object.getPrototypeOf(prototype) === null;
}
function isDisjoint(...headers) {
  const parameters = /* @__PURE__ */ new Set();
  for (const header of headers)
    if (header)
      for (const parameter of Object.keys(header)) {
        if (parameters.has(parameter))
          return false;
        parameters.add(parameter);
      }
  return true;
}
function assertNotSet(value, name) {
  if (value !== void 0)
    throw new TypeError(`${name} can only be called once`);
}
function decodeBase64url(value, label, ErrorClass) {
  try {
    return decode(value);
  } catch (cause) {
    throw new ErrorClass(`Failed to base64url decode the ${label}`, { cause });
  }
}
function encodeAsciiMember(value, label, ErrorClass) {
  try {
    return encode(value);
  } catch (cause) {
    throw new ErrorClass(`The ${label} is not a valid base64url string`, { cause });
  }
}
function parseJoseHeader(encodedProtectedHeader, ErrorClass, message2) {
  let parsed;
  try {
    parsed = JSON.parse(strictDecoder.decode(decode(encodedProtectedHeader)));
  } catch (cause) {
    throw new ErrorClass(message2, { cause });
  }
  if (!isObject(parsed))
    throw new ErrorClass(message2);
  return parsed;
}
var JWS_RECOGNIZED = { __proto__: null, b64: true };
function validateAlgorithms(option, algorithms) {
  if (algorithms !== void 0 && (!Array.isArray(algorithms) || algorithms.some((s) => typeof s != "string")))
    throw new TypeError(`"${option}" option must be an array of strings`);
  return algorithms === void 0 ? void 0 : new Set(algorithms);
}
function validateCritDuplicates(Err, protectedHeader) {
  const { crit } = protectedHeader ?? {};
  if (Array.isArray(crit) && new Set(crit).size !== crit.length)
    throw new Err('"crit" (Critical) Header Parameter MUST NOT contain duplicate values');
}
function validateCrit(Err, recognizedDefault, recognizedOption, protectedHeader, joseHeader) {
  if (joseHeader.crit !== void 0 && protectedHeader?.crit === void 0)
    throw new Err('"crit" (Critical) Header Parameter MUST be integrity protected');
  if (!protectedHeader || protectedHeader.crit === void 0)
    return [];
  if (!Array.isArray(protectedHeader.crit) || protectedHeader.crit.length === 0 || protectedHeader.crit.some((input) => typeof input != "string" || input.length === 0))
    throw new Err('"crit" (Critical) Header Parameter MUST be an array of non-empty strings when present');
  const recognized = recognizedOption === void 0 ? recognizedDefault : { __proto__: null, ...recognizedOption, ...recognizedDefault };
  for (const parameter of protectedHeader.crit) {
    if (!(parameter in recognized))
      throw new JOSENotSupported(`Extension Header Parameter "${parameter}" is not recognized`);
    if (!Object.hasOwn(joseHeader, parameter) || joseHeader[parameter] === void 0)
      throw new Err(`Extension Header Parameter "${parameter}" is missing`);
    if (recognized[parameter] && (!Object.hasOwn(protectedHeader, parameter) || protectedHeader[parameter] === void 0))
      throw new Err(`Extension Header Parameter "${parameter}" MUST be integrity protected`);
  }
  return protectedHeader.crit;
}
function validateB64(protectedHeader, extensions) {
  if (extensions.includes("b64")) {
    const b64 = protectedHeader.b64;
    if (typeof b64 != "boolean")
      throw new JWSInvalid('The "b64" (base64url-encode payload) Header Parameter must be a boolean');
    return b64;
  }
  return true;
}
function serializeJoseHeader(Err, header) {
  let serialized, parsed;
  try {
    serialized = JSON.stringify(header), parsed = JSON.parse(serialized);
  } catch (cause) {
    throw new Err("JOSE Header is not valid JSON", { cause });
  }
  if (!isObject(parsed))
    throw new Err("JOSE Header is not a JSON object");
  return [parsed, serialized];
}

// node_modules/jose/dist/webapi/lib/key.js
var tag = (key) => key[Symbol.toStringTag];
var jwkMatchesOp = (entry, key, usage) => {
  const { alg } = entry;
  if (key.use !== void 0) {
    const expected = usage === "sign" || usage === "verify" ? "sig" : "enc";
    if (key.use !== expected)
      throw new TypeError(`Invalid key for this operation, its "use" must be "${expected}" when present`);
  }
  if (key.alg !== void 0 && key.alg !== alg)
    throw new TypeError(`Invalid key for this operation, its "alg" must be "${alg}" when present`);
  if (Array.isArray(key.key_ops)) {
    const expectedKeyOp = usage === "encrypt" || usage === "decrypt" ? entry.ops?.[usage === "encrypt" ? 0 : 1] : usage;
    if (expectedKeyOp && !key.key_ops.includes(expectedKeyOp))
      throw new TypeError(`Invalid key for this operation, its "key_ops" must include "${expectedKeyOp}" when present`);
  }
};
async function prepareKey(entry, key, usage) {
  const { alg, secret } = entry, requiresPrivateKey = usage === "decrypt" || usage === "sign";
  if (secret && key instanceof Uint8Array)
    return key;
  let normalized, keyObject;
  if (isObject(key)) {
    if (normalized = normalizeJwk(key), typeof normalized.kty != "string")
      throw invalidKeyType(alg, key, secret);
    if (!(secret ? normalized.kty === "oct" && typeof normalized.k == "string" : normalized.kty !== "oct" && (requiresPrivateKey ? normalized.kty === "AKP" && typeof normalized.priv == "string" || typeof normalized.d == "string" : normalized.d === void 0 && normalized.priv === void 0)))
      throw new TypeError(secret ? 'JSON Web Key for symmetric algorithms must have JWK "kty" (Key Type) equal to "oct" and the JWK "k" (Key Value) present' : `JSON Web Key for this operation must be a ${requiresPrivateKey ? "private" : "public"} JWK`);
    if (jwkMatchesOp(entry, normalized, usage), normalized.kty === "oct")
      return decode(normalized.k);
    if (!Object.isFrozen(key)) {
      const { key_ops } = key;
      Array.isArray(key_ops) && Object.freeze(key_ops), Object.freeze(key);
    }
  } else {
    if (!isKeyLike(key))
      throw invalidKeyType(alg, key, secret);
    const expectedType = secret ? "secret" : requiresPrivateKey ? "private" : "public";
    if (key.type !== expectedType && (secret || ["secret", "public", "private"].includes(key.type)))
      throw new TypeError(`${tag(key)} instances must be of type "${expectedType}" for the ${alg} algorithm`);
    if (isCryptoKey(key))
      return key;
    if (keyObject = key, keyObject.type === "secret")
      return keyObject.export();
  }
  cache ||= /* @__PURE__ */ new WeakMap();
  const cacheKey = key;
  let cached = cache.get(cacheKey);
  if (cached?.[alg])
    return cached[alg];
  if (cached || cache.set(cacheKey, cached = {}), keyObject && typeof keyObject.toCryptoKey == "function") {
    const isPublic = keyObject.type === "public", crv = nist[keyObject.asymmetricKeyDetails?.namedCurve], params = entry.resolve?.({ crv, asymmetricKeyType: keyObject.asymmetricKeyType }) ?? entry.subtle;
    return cached[alg] = keyObject.toCryptoKey(params, isPublic, entry.usages[isPublic ? 0 : 1]);
  }
  return normalized ??= keyObject.export({ format: "jwk" }), normalized.alg = alg, cached[alg] = await jwkToKey(entry, normalized);
}
var cache;
var nist = {
  __proto__: null,
  prime256v1: "P-256",
  secp384r1: "P-384",
  secp521r1: "P-521"
};
var isCryptoKey = (key) => {
  if (key?.[Symbol.toStringTag] === "CryptoKey")
    return true;
  try {
    return key instanceof CryptoKey;
  } catch {
    return false;
  }
};
var isKeyObject = (key) => key?.[Symbol.toStringTag] === "KeyObject";
var isKeyLike = (key) => isCryptoKey(key) || isKeyObject(key);
function message(msg, actual, ...types) {
  if (types.length > 2) {
    const last = types.pop();
    msg += `one of type ${types.join(", ")}, or ${last}.`;
  } else types.length === 2 ? msg += `one of type ${types[0]} or ${types[1]}.` : msg += `of type ${types[0]}.`;
  return actual == null ? msg += ` Received ${actual}` : typeof actual == "function" && actual.name ? msg += ` Received function ${actual.name}` : typeof actual == "object" && actual != null && actual.constructor?.name && (msg += ` Received an instance of ${actual.constructor.name}`), msg;
}
function invalidKeyType(alg, actual, secret) {
  const types = ["CryptoKey", "KeyObject", "JSON Web Key"];
  return secret && types.push("Uint8Array"), new TypeError(message(`Key for the ${alg} algorithm must be `, actual, ...types));
}
var unusable = (name, prop = "algorithm.name") => new TypeError(`CryptoKey does not support this operation, its ${prop} must be ${name}`);
function checkUsage(key, usage) {
  if (usage && !key.usages.includes(usage))
    throw new TypeError(`CryptoKey does not support this operation, its usages must include ${usage}.`);
}
function checkModulusLength(alg, key) {
  const { modulusLength } = key.algorithm;
  if (typeof modulusLength != "number" || modulusLength < 2048)
    throw new TypeError(`${alg} requires key modulusLength to be 2048 bits or larger`);
}
function checkCryptoKey(key, expected, usage) {
  const algorithm = key.algorithm;
  if (algorithm.name !== expected.name)
    throw unusable(expected.name);
  if (expected.hash && algorithm.hash?.name !== expected.hash)
    throw unusable(expected.hash, "algorithm.hash");
  if (expected.namedCurve && algorithm.namedCurve !== expected.namedCurve)
    throw unusable(expected.namedCurve, "algorithm.namedCurve");
  if (expected.length !== void 0 && algorithm.length !== expected.length)
    throw unusable(expected.length, "algorithm.length");
  checkUsage(key, usage);
}
function snapshotJwk(jwk) {
  return { __proto__: null, ...jwk };
}
function normalizeJwk(jwk) {
  const normalized = snapshotJwk(jwk);
  if (normalized.ext !== void 0 && typeof normalized.ext != "boolean")
    throw new TypeError('"ext" (Extractable) Parameter must be a boolean');
  if (normalized.key_ops !== void 0) {
    const value = normalized.key_ops, keyOps = Array.isArray(value) ? [...value] : void 0;
    if (!keyOps || keyOps.some((operation) => typeof operation != "string") || new Set(keyOps).size !== keyOps.length)
      throw new TypeError('"key_ops" (Key Operations) Parameter must be an array of unique strings');
    normalized.key_ops = keyOps;
  }
  return normalized;
}
async function jwkToKey(entry, jwk, extractable) {
  if (!entry.kty.includes(jwk.kty))
    throw new JOSENotSupported('Invalid or unsupported JWK "alg" (Algorithm) Parameter value');
  const algorithm = entry.resolve?.({ kty: jwk.kty, crv: jwk.crv }) ?? entry.subtle, isPrivate = !!(jwk.d || jwk.priv), keyData = { ...jwk, ext: extractable ?? jwk.ext };
  return keyData.kty !== "AKP" && delete keyData.alg, delete keyData.use, crypto.subtle.importKey("jwk", keyData, algorithm, keyData.ext ?? !isPrivate, jwk.key_ops ?? entry.usages[isPrivate ? 1 : 0]);
}
async function rawKey(key, expected, usage, extractable = false) {
  return key instanceof Uint8Array && (key = await crypto.subtle.importKey("raw", key, expected, extractable, [usage])), checkCryptoKey(key, expected, usage), key;
}

// node_modules/jose/dist/webapi/lib/key_descriptor.js
function table(entries) {
  const out = { __proto__: null };
  for (const alg in entries)
    out[alg] = { ...entries[alg], alg };
  return out;
}

// node_modules/jose/dist/webapi/lib/jws_algorithms.js
var sig = [["verify"], ["sign"]];
function hmac(bits) {
  const subtle = { name: "HMAC", hash: `SHA-${bits}` };
  return { kty: ["oct"], secret: true, subtle, signing: subtle, usages: sig };
}
function rsa(bits, saltLength) {
  const subtle = { name: saltLength ? "RSA-PSS" : "RSASSA-PKCS1-v1_5", hash: `SHA-${bits}` };
  return {
    kty: ["RSA"],
    subtle,
    signing: saltLength ? { ...subtle, saltLength } : subtle,
    usages: sig,
    minRsaBits: 2048
  };
}
function ecdsa(crv, bits) {
  return {
    kty: ["EC"],
    crv,
    subtle: { name: "ECDSA", namedCurve: crv },
    signing: { name: "ECDSA", hash: `SHA-${bits}` },
    usages: sig
  };
}
function eddsa() {
  const subtle = { name: "Ed25519" };
  return {
    kty: ["OKP"],
    crv: "Ed25519",
    subtle,
    signing: subtle,
    usages: sig
  };
}
function mldsa(parameterSet) {
  const subtle = { name: `ML-DSA-${parameterSet}` };
  return {
    kty: ["AKP"],
    subtle,
    signing: subtle,
    usages: sig
  };
}
var JWS = table({
  HS256: hmac(256),
  HS384: hmac(384),
  HS512: hmac(512),
  RS256: rsa(256),
  RS384: rsa(384),
  RS512: rsa(512),
  PS256: rsa(256, 32),
  PS384: rsa(384, 48),
  PS512: rsa(512, 64),
  ES256: ecdsa("P-256", 256),
  ES384: ecdsa("P-384", 384),
  ES512: ecdsa("P-521", 512),
  EdDSA: eddsa(),
  Ed25519: eddsa(),
  "ML-DSA-44": mldsa(44),
  "ML-DSA-65": mldsa(65),
  "ML-DSA-87": mldsa(87)
});
function jwsAlgorithm(alg) {
  const entry = typeof alg == "string" ? JWS[alg] : void 0;
  if (!entry)
    throw new JOSENotSupported(`alg ${alg} is not supported either by JOSE or your javascript runtime`);
  return entry;
}

// node_modules/jose/dist/webapi/lib/jws_verify.js
function prepareVerify(options) {
  return [options && validateAlgorithms("algorithms", options.algorithms), options?.crit];
}
function parseProtectedHeader(encodedProtectedHeader) {
  return encodedProtectedHeader === void 0 ? {} : parseJoseHeader(encodedProtectedHeader, JWSInvalid, "JWS Protected Header is invalid");
}
function encodeCompactUnencodedPayload(payload) {
  try {
    return encode(payload);
  } catch (cause) {
    throw new JWSInvalid("JWS Compact Serialization payload must use only ASCII characters", {
      cause
    });
  }
}
async function verifySignature(jws, shared, key, encodeUnencodedPayload, parsedProtected) {
  const { protected: encodedProtectedHeader, header, payload: inputPayload } = jws, protectedHeader = parsedProtected ?? parseProtectedHeader(encodedProtectedHeader);
  if (!isDisjoint(protectedHeader, header))
    throw new JWSInvalid("JWS Protected and JWS Unprotected Header Parameter names must be disjoint");
  const joseHeader = { ...protectedHeader, ...header }, b64 = validateB64(protectedHeader, validateCrit(JWSInvalid, JWS_RECOGNIZED, shared[1], protectedHeader, joseHeader)), { alg } = joseHeader;
  if (typeof alg != "string" || !alg)
    throw new JWSInvalid('JWS "alg" (Algorithm) Header Parameter missing or invalid');
  if (shared[0] && !shared[0].has(alg))
    throw new JOSEAlgNotAllowed('"alg" (Algorithm) Header Parameter value not allowed');
  if (b64) {
    if (typeof inputPayload != "string")
      throw new JWSInvalid("JWS Payload must be a string");
  } else if (typeof inputPayload != "string" && !(inputPayload instanceof Uint8Array))
    throw new JWSInvalid("JWS Payload must be a string or an Uint8Array instance");
  const signingPayload = b64 || typeof inputPayload != "string" ? inputPayload : encodeUnencodedPayload(inputPayload);
  let resolvedKey = false;
  typeof key == "function" && (key = await key(protectedHeader, jws), resolvedKey = true);
  const entry = jwsAlgorithm(alg), signingInput = concat(encodedProtectedHeader !== void 0 ? encode(encodedProtectedHeader) : new Uint8Array(), encode("."), typeof signingPayload == "string" ? shared[2] ??= encodeAsciiMember(signingPayload, "payload", JWSInvalid) : signingPayload), signature = decodeBase64url(jws.signature, "signature", JWSInvalid), verificationKey = await prepareKey(entry, key, "verify"), cryptoKey = await rawKey(verificationKey, entry.subtle, "verify");
  entry.minRsaBits && checkModulusLength(entry.alg, cryptoKey);
  let verified = false;
  try {
    verified = await crypto.subtle.verify(entry.signing, cryptoKey, signature, signingInput);
  } catch (cause) {
    throw new JWSSignatureVerificationFailed(void 0, { cause });
  }
  if (!verified)
    throw new JWSSignatureVerificationFailed();
  const result = { payload: typeof signingPayload == "string" ? decodeBase64url(signingPayload, "payload", JWSInvalid) : signingPayload };
  return encodedProtectedHeader !== void 0 && (result.protectedHeader = protectedHeader), header !== void 0 && (result.unprotectedHeader = header), resolvedKey ? [{ ...result, key: verificationKey }, b64] : [result, b64];
}
async function verifyCompact(jws, shared, key) {
  if (jws instanceof Uint8Array && (jws = decoder.decode(jws)), typeof jws != "string")
    throw new JWSInvalid("Compact JWS must be a string or Uint8Array");
  const { 0: protectedHeader, 1: payload, 2: signature, length } = jws.split(".");
  if (length !== 3)
    throw new JWSInvalid("Invalid Compact JWS");
  return verifySignature({ payload, protected: protectedHeader, signature }, shared, key, encodeCompactUnencodedPayload);
}

// node_modules/jose/dist/webapi/lib/jwt_claims_set.js
var epoch = (date) => Math.floor(date.getTime() / 1e3);
var multipliers = {
  s: 1,
  m: 60,
  h: 3600,
  d: 86400,
  w: 604800,
  y: 31557600
};
var REGEX = /^(\+|\-)? ?(\d+|\d+\.\d+) ?(seconds?|secs?|s|minutes?|mins?|m|hours?|hrs?|h|days?|d|weeks?|w|years?|yrs?|y)(?: (ago|from now))?$/i;
var checkFailed = "check_failed";
function invalidDuration() {
  throw new TypeError("Invalid time period format");
}
function secs(str) {
  typeof str != "string" && invalidDuration();
  const matched = REGEX.exec(str);
  (!matched || matched[4] && matched[1]) && invalidDuration();
  const value = parseFloat(matched[2]), durationSeconds = Math.round(value * multipliers[matched[3][0].toLowerCase()]);
  return Number.isFinite(durationSeconds) || invalidDuration(), matched[1] === "-" || matched[4] === "ago" ? -durationSeconds : durationSeconds;
}
function validateInput(label, input) {
  if (!Number.isFinite(input))
    throw new TypeError(`Invalid ${label} input`);
  return input;
}
function validateStringClaim(claim, value) {
  if (typeof value != "string")
    throw new TypeError(`"${claim}" claim must be a string`);
}
function validateAudienceClaim(value) {
  if (typeof value != "string" && (!Array.isArray(value) || Array.from(value).some((member) => typeof member != "string")))
    throw new TypeError('"aud" claim must be a string or an array of strings');
}
function numericDate(value, label) {
  return typeof value == "number" ? validateInput(label, value) : value instanceof Date ? validateInput(label, epoch(value)) : epoch(/* @__PURE__ */ new Date()) + secs(value);
}
var normalizeTyp = (value) => {
  const normalized = value.toLowerCase();
  return value.includes("/") ? normalized : `application/${normalized}`;
};
var checkAudiencePresence = (audPayload, audOption) => typeof audPayload == "string" ? audOption.includes(audPayload) : Array.isArray(audPayload) ? audOption.some((aud) => audPayload.includes(aud)) : false;
function validateNumericDate(claimsSet, claim, required = false) {
  const value = claimsSet[claim];
  if (!(value === void 0 && !required)) {
    if (typeof value != "number")
      throw new JWTClaimValidationFailed(`"${claim}" claim must be a number`, claimsSet, claim, "invalid");
    return value;
  }
}
function unexpectedClaim(claimsSet, claim) {
  throw new JWTClaimValidationFailed(`unexpected "${claim}" claim value`, claimsSet, claim, checkFailed);
}
function validateClaimsSet(protectedHeader, claimsSetBytes, options = {}) {
  let claimsSet;
  try {
    claimsSet = JSON.parse(strictDecoder.decode(claimsSetBytes));
  } catch (cause) {
    throw new JWTInvalid("JWT Claims Set must be a top-level JSON object", { cause });
  }
  if (!isObject(claimsSet))
    throw new JWTInvalid("JWT Claims Set must be a top-level JSON object");
  const { typ } = options;
  if (typ !== void 0 && (typeof protectedHeader.typ != "string" || normalizeTyp(protectedHeader.typ) !== normalizeTyp(typ)))
    throw new JWTClaimValidationFailed('unexpected "typ" JWT header value', claimsSet, "typ", checkFailed);
  const { requiredClaims = [], issuer, subject, audience, maxTokenAge } = options, presenceCheck = [...requiredClaims];
  maxTokenAge !== void 0 && presenceCheck.push("iat"), audience !== void 0 && presenceCheck.push("aud"), subject !== void 0 && presenceCheck.push("sub"), issuer !== void 0 && presenceCheck.push("iss");
  for (const claim of new Set(presenceCheck.reverse()))
    if (!Object.hasOwn(claimsSet, claim))
      throw new JWTClaimValidationFailed(`missing required "${claim}" claim`, claimsSet, claim, "missing");
  issuer !== void 0 && !(Array.isArray(issuer) ? issuer : [issuer]).includes(claimsSet.iss) && unexpectedClaim(claimsSet, "iss"), subject !== void 0 && claimsSet.sub !== subject && unexpectedClaim(claimsSet, "sub"), audience !== void 0 && !checkAudiencePresence(claimsSet.aud, typeof audience == "string" ? [audience] : audience) && unexpectedClaim(claimsSet, "aud");
  const { clockTolerance } = options;
  let tolerance = 0;
  if (typeof clockTolerance == "string")
    tolerance = secs(clockTolerance);
  else if (clockTolerance !== void 0) {
    if (typeof clockTolerance != "number")
      throw new TypeError("Invalid clockTolerance option type");
    tolerance = clockTolerance;
  }
  if (validateInput("clockTolerance option", tolerance), tolerance < 0)
    throw new TypeError("Invalid clockTolerance option input");
  const { currentDate } = options, now = validateInput("currentDate option", epoch(currentDate === void 0 ? /* @__PURE__ */ new Date() : currentDate)), iat = validateNumericDate(claimsSet, "iat", maxTokenAge !== void 0), nbf = validateNumericDate(claimsSet, "nbf");
  if (nbf !== void 0 && nbf > now + tolerance)
    throw new JWTClaimValidationFailed('"nbf" claim timestamp check failed', claimsSet, "nbf", checkFailed);
  const exp = validateNumericDate(claimsSet, "exp");
  if (exp !== void 0 && exp <= now - tolerance)
    throw new JWTExpired('"exp" claim timestamp check failed', claimsSet, "exp", checkFailed);
  if (maxTokenAge !== void 0) {
    const age = now - iat, max = validateInput("maxTokenAge option", typeof maxTokenAge == "number" ? maxTokenAge : secs(maxTokenAge));
    if (age - tolerance > max)
      throw new JWTExpired('"iat" claim timestamp check failed (too far in the past)', claimsSet, "iat", checkFailed);
    if (age < -tolerance)
      throw new JWTClaimValidationFailed('"iat" claim timestamp check failed (it should be in the past)', claimsSet, "iat", checkFailed);
  }
  return claimsSet;
}
var producerClaimsSets;
function producerClaimsSet(producer) {
  return producerClaimsSets.get(producer);
}
function jwtClaimsSetBytes(producer) {
  const claimsSet = producerClaimsSet(producer);
  for (const claim of ["iat", "nbf", "exp"]) {
    const value = claimsSet[claim];
    if (typeof value == "number" && !Number.isFinite(value))
      throw new TypeError(`"${claim}" claim must be a finite number`);
  }
  return encoder.encode(JSON.stringify(claimsSet));
}
var JWTClaimsBuilder = class {
  constructor(claimsSet = {}) {
    if (!isObject(claimsSet))
      throw new TypeError("JWT Claims Set MUST be an object");
    (producerClaimsSets ||= /* @__PURE__ */ new WeakMap()).set(this, structuredClone(claimsSet));
  }
  setIssuer(value) {
    return validateStringClaim("iss", value), producerClaimsSet(this).iss = value, this;
  }
  setSubject(value) {
    return validateStringClaim("sub", value), producerClaimsSet(this).sub = value, this;
  }
  setAudience(value) {
    return validateAudienceClaim(value), producerClaimsSet(this).aud = value, this;
  }
  setJti(value) {
    return validateStringClaim("jti", value), producerClaimsSet(this).jti = value, this;
  }
  setNotBefore(value) {
    return producerClaimsSet(this).nbf = numericDate(value, "setNotBefore"), this;
  }
  setExpirationTime(value) {
    return producerClaimsSet(this).exp = numericDate(value, "setExpirationTime"), this;
  }
  setIssuedAt(value) {
    const claimsSet = producerClaimsSet(this);
    return value === void 0 ? claimsSet.iat = epoch(/* @__PURE__ */ new Date()) : typeof value == "string" ? claimsSet.iat = validateInput("setIssuedAt", epoch(/* @__PURE__ */ new Date()) + secs(value)) : claimsSet.iat = numericDate(value, "setIssuedAt"), this;
  }
};

// node_modules/jose/dist/webapi/jwt/verify.js
async function jwtVerify(jwt, key, options) {
  const [verified, b64] = await verifyCompact(jwt, prepareVerify(options), key);
  if (!b64)
    throw new JWTInvalid("JWTs MUST NOT use unencoded payload");
  const payload = validateClaimsSet(verified.protectedHeader, verified.payload, options);
  return { ...verified, payload };
}

// node_modules/jose/dist/webapi/lib/jws_sign.js
async function createSignature(input, key, rejectUnencoded) {
  let [payload, protectedHeader, unprotectedHeader, crit] = input, encodedProtectedHeader = "";
  if (protectedHeader !== void 0) {
    const normalized = serializeJoseHeader(JWSInvalid, protectedHeader);
    protectedHeader = normalized[0], encodedProtectedHeader = encode2(normalized[1]);
  }
  if (unprotectedHeader !== void 0 && (unprotectedHeader = serializeJoseHeader(JWSInvalid, unprotectedHeader)[0]), !protectedHeader && !unprotectedHeader)
    throw new JWSInvalid("either setProtectedHeader or setUnprotectedHeader must be called before #sign()");
  if (!isDisjoint(protectedHeader, unprotectedHeader))
    throw new JWSInvalid("JWS Protected and JWS Unprotected Header Parameter names must be disjoint");
  const joseHeader = { ...protectedHeader, ...unprotectedHeader };
  validateCritDuplicates(JWSInvalid, protectedHeader);
  const b64 = validateB64(protectedHeader, validateCrit(JWSInvalid, JWS_RECOGNIZED, crit, protectedHeader, joseHeader));
  b64 || rejectUnencoded?.();
  const { alg } = joseHeader;
  if (typeof alg != "string" || !alg)
    throw new JWSInvalid('JWS "alg" (Algorithm) Header Parameter missing or invalid');
  const entry = jwsAlgorithm(alg);
  let encodedPayload = "", signingPayload = payload, signingInput;
  if (b64) {
    const encodedPayloadCache = input[4];
    encodedPayloadCache ? (encodedPayload = encodedPayloadCache[0] ??= encode2(payload), signingPayload = encodedPayloadCache[1] ??= encode(encodedPayload)) : (encodedPayload = encode2(payload), signingInput = encoder.encode(`${encodedProtectedHeader}.${encodedPayload}`));
  }
  signingInput ??= concat(encode(encodedProtectedHeader), encode("."), signingPayload);
  const signingKey = await rawKey(await prepareKey(entry, key, "sign"), entry.subtle, "sign");
  entry.minRsaBits && checkModulusLength(entry.alg, signingKey);
  const jws = {
    signature: encode2(new Uint8Array(await crypto.subtle.sign(entry.signing, signingKey, signingInput))),
    payload: encodedPayload
  };
  return protectedHeader && (jws.protected = encodedProtectedHeader), unprotectedHeader && (jws.header = unprotectedHeader), [jws, b64];
}
async function createCompactSignature(payload, protectedHeader, crit, key, rejectUnencoded) {
  const [jws] = await createSignature([payload, protectedHeader, void 0, crit], key, rejectUnencoded);
  return `${jws.protected}.${jws.payload}.${jws.signature}`;
}

// node_modules/jose/dist/webapi/jwt/sign.js
var SignJWT_base = JWTClaimsBuilder;
var SignJWT = class extends SignJWT_base {
  #protectedHeader;
  setProtectedHeader(protectedHeader) {
    return assertNotSet(this.#protectedHeader, "setProtectedHeader"), this.#protectedHeader = protectedHeader, this;
  }
  async sign(key, options) {
    return createCompactSignature(jwtClaimsSetBytes(this), this.#protectedHeader, options?.crit, key, () => {
      throw new JWTInvalid("JWTs MUST NOT use unencoded payload");
    });
  }
};

// functions/api/services/crypto.ts
var PBKDF2_ITERATIONS = 1e5;
var KEY_LEN_BYTES = 32;
function bufferToHex(buffer) {
  const bytes = new Uint8Array(buffer);
  let hex = "";
  for (let i = 0; i < bytes.length; i++) {
    hex += bytes[i].toString(16).padStart(2, "0");
  }
  return hex;
}
function hexToBuffer(hex) {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) {
    bytes[i / 2] = parseInt(hex.substring(i, i + 2), 16);
  }
  return bytes;
}
async function hashPassword(password) {
  const enc = new TextEncoder();
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const keyMaterial = await crypto.subtle.importKey(
    "raw",
    enc.encode(password),
    { name: "PBKDF2" },
    false,
    ["deriveBits"]
  );
  const derivedBits = await crypto.subtle.deriveBits(
    {
      name: "PBKDF2",
      salt,
      iterations: PBKDF2_ITERATIONS,
      hash: "SHA-256"
    },
    keyMaterial,
    KEY_LEN_BYTES * 8
  );
  const saltHex = bufferToHex(salt.buffer);
  const hashHex = bufferToHex(derivedBits);
  return `${saltHex}:${hashHex}`;
}
async function verifyPassword(password, storedHash) {
  const parts = storedHash.split(":");
  if (parts.length !== 2) return false;
  const [saltHex, expectedHashHex] = parts;
  const salt = hexToBuffer(saltHex);
  const enc = new TextEncoder();
  const keyMaterial = await crypto.subtle.importKey(
    "raw",
    enc.encode(password),
    { name: "PBKDF2" },
    false,
    ["deriveBits"]
  );
  const derivedBits = await crypto.subtle.deriveBits(
    {
      name: "PBKDF2",
      salt,
      iterations: PBKDF2_ITERATIONS,
      hash: "SHA-256"
    },
    keyMaterial,
    KEY_LEN_BYTES * 8
  );
  const actualHashHex = bufferToHex(derivedBits);
  return actualHashHex === expectedHashHex;
}
var DEFAULT_JWT_SECRET = "english-word-dev-secret-key-32chars-min!!";
async function signJwt(payload, secret = DEFAULT_JWT_SECRET, expiresIn = "7d") {
  const secretKey = new TextEncoder().encode(secret || DEFAULT_JWT_SECRET);
  return new SignJWT(payload).setProtectedHeader({ alg: "HS256" }).setIssuedAt().setExpirationTime(expiresIn).sign(secretKey);
}
async function verifyJwt(token, secret = DEFAULT_JWT_SECRET) {
  try {
    const secretKey = new TextEncoder().encode(secret || DEFAULT_JWT_SECRET);
    const { payload } = await jwtVerify(token, secretKey);
    return payload;
  } catch {
    return null;
  }
}

// functions/api/middleware/auth.ts
async function authMiddleware(c, next) {
  const authHeader = c.req.header("Authorization");
  if (!authHeader || !authHeader.startsWith("Bearer ")) {
    return c.json({ error: "UNAUTHORIZED", message: "\u8BF7\u5148\u767B\u5F55" }, 401);
  }
  const token = authHeader.substring(7).trim();
  const secret = c.env.JWT_SECRET;
  const user = await verifyJwt(token, secret);
  if (!user) {
    return c.json({ error: "INVALID_TOKEN", message: "\u767B\u5F55\u6001\u65E0\u6548\u6216\u5DF2\u8FC7\u671F" }, 401);
  }
  c.set("user", user);
  await next();
}
function requireRole(...allowedRoles) {
  return async (c, next) => {
    const user = c.get("user");
    if (!user) {
      return c.json({ error: "UNAUTHORIZED", message: "\u8BF7\u5148\u767B\u5F55" }, 401);
    }
    if (!allowedRoles.includes(user.role)) {
      return c.json({ error: "FORBIDDEN", message: "\u65E0\u6743\u9650\u8BBF\u95EE\u8BE5\u8D44\u6E90" }, 403);
    }
    await next();
  };
}

// functions/api/routes/auth.ts
var authRouter = new Hono3();
async function ensureAdminUser(db) {
  const existing = await db.prepare("SELECT id FROM users WHERE username = ?").bind("admin").first();
  if (!existing) {
    const adminId = crypto.randomUUID();
    const passwordHash = await hashPassword("P@ss=w0rd");
    const now = Date.now();
    await db.prepare(
      "INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"
    ).bind(adminId, "admin", "\u7CFB\u7EDF\u7BA1\u7406\u5458", passwordHash, "admin", now, now).run();
  }
}
authRouter.post("/login", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { username, password } = body;
  if (!username || !password) {
    return c.json({ error: "BAD_REQUEST", message: "\u7528\u6237\u540D\u548C\u5BC6\u7801\u4E0D\u80FD\u4E3A\u7A7A" }, 400);
  }
  const db = c.env.DB;
  if (!db) {
    return c.json({ error: "DB_ERROR", message: "\u6570\u636E\u5E93\u672A\u8FDE\u63A5" }, 500);
  }
  if (username === "admin") {
    await ensureAdminUser(db);
  }
  const user = await db.prepare("SELECT id, username, display_name, password_hash, role FROM users WHERE username = ?").bind(username.trim()).first();
  if (!user) {
    return c.json({ error: "INVALID_CREDENTIALS", message: "\u7528\u6237\u540D\u6216\u5BC6\u7801\u9519\u8BEF" }, 401);
  }
  const isMatch = await verifyPassword(password, user.password_hash);
  if (!isMatch) {
    return c.json({ error: "INVALID_CREDENTIALS", message: "\u7528\u6237\u540D\u6216\u5BC6\u7801\u9519\u8BEF" }, 401);
  }
  const authUser = {
    id: user.id,
    username: user.username,
    displayName: user.display_name,
    role: user.role
  };
  const token = await signJwt(authUser, c.env.JWT_SECRET);
  return c.json({
    token,
    user: authUser
  });
});
authRouter.get("/me", authMiddleware, async (c) => {
  const user = c.get("user");
  return c.json({ user });
});
authRouter.post("/change-password", authMiddleware, async (c) => {
  const user = c.get("user");
  const body = await c.req.json().catch(() => ({}));
  const { currentPassword, newPassword } = body;
  if (!currentPassword || !newPassword) {
    return c.json({ error: "BAD_REQUEST", message: "\u539F\u5BC6\u7801\u548C\u65B0\u5BC6\u7801\u4E0D\u80FD\u4E3A\u7A7A" }, 400);
  }
  if (newPassword.length < 6) {
    return c.json({ error: "BAD_REQUEST", message: "\u65B0\u5BC6\u7801\u957F\u5EA6\u81F3\u5C11\u9700\u8981 6 \u4E2A\u5B57\u7B26" }, 400);
  }
  const db = c.env.DB;
  const dbUser = await db.prepare("SELECT password_hash FROM users WHERE id = ?").bind(user.id).first();
  if (!dbUser) {
    return c.json({ error: "USER_NOT_FOUND", message: "\u7528\u6237\u4E0D\u5B58\u5728" }, 404);
  }
  const isMatch = await verifyPassword(currentPassword, dbUser.password_hash);
  if (!isMatch) {
    return c.json({ error: "INVALID_CURRENT_PASSWORD", message: "\u539F\u5BC6\u7801\u9519\u8BEF" }, 400);
  }
  const newHash = await hashPassword(newPassword);
  const now = Date.now();
  await db.prepare("UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?").bind(newHash, now, user.id).run();
  return c.json({ success: true, message: "\u5BC6\u7801\u4FEE\u6539\u6210\u529F" });
});

// functions/api/routes/admin.ts
var adminRouter = new Hono3();
adminRouter.use("*", authMiddleware, requireRole("admin"));
var DEFAULT_NEW_USER_PASSWORD = "passw0rd";
adminRouter.get("/teachers", async (c) => {
  const db = c.env.DB;
  const teachers = await db.prepare(
    `SELECT u.id, u.username, u.display_name as displayName, u.created_at as createdAt,
              (SELECT COUNT(*) FROM teacher_students ts WHERE ts.teacher_id = u.id) as studentCount,
              (SELECT COUNT(*) FROM teacher_words tw WHERE tw.teacher_id = u.id) as wordCount
       FROM users u
       WHERE u.role = 'teacher'
       ORDER BY u.created_at DESC`
  ).all();
  return c.json({ teachers: teachers.results });
});
adminRouter.post("/teachers", async (c) => {
  const db = c.env.DB;
  const body = await c.req.json().catch(() => ({}));
  const { username, displayName } = body;
  if (!username) {
    return c.json({ error: "BAD_REQUEST", message: "\u7528\u6237\u540D\u4E0D\u80FD\u4E3A\u7A7A" }, 400);
  }
  const trimmedUsername = username.trim();
  const existing = await db.prepare("SELECT id FROM users WHERE username = ?").bind(trimmedUsername).first();
  if (existing) {
    return c.json({ error: "USERNAME_EXISTS", message: "\u8BE5\u7528\u6237\u540D\u5DF2\u5B58\u5728" }, 409);
  }
  const teacherId = crypto.randomUUID();
  const passwordHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();
  await db.prepare(
    "INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"
  ).bind(
    teacherId,
    trimmedUsername,
    displayName?.trim() || trimmedUsername,
    passwordHash,
    "teacher",
    now,
    now
  ).run();
  return c.json({
    teacher: {
      id: teacherId,
      username: trimmedUsername,
      displayName: displayName?.trim() || trimmedUsername,
      role: "teacher",
      studentCount: 0,
      wordCount: 0,
      createdAt: now
    }
  });
});
adminRouter.delete("/teachers/:teacherId", async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param("teacherId");
  const deleteOrphanStudents = c.req.query("deleteOrphanStudents") === "true";
  const teacher = await db.prepare("SELECT id FROM users WHERE id = ? AND role = 'teacher'").bind(teacherId).first();
  if (!teacher) {
    return c.json({ error: "NOT_FOUND", message: "\u6559\u5E08\u4E0D\u5B58\u5728" }, 404);
  }
  const boundStudents = await db.prepare("SELECT student_id FROM teacher_students WHERE teacher_id = ?").bind(teacherId).all();
  const orphanStudentIds = [];
  for (const s of boundStudents.results) {
    const otherTeacherCount = await db.prepare("SELECT COUNT(*) as count FROM teacher_students WHERE student_id = ? AND teacher_id != ?").bind(s.student_id, teacherId).first();
    if (!otherTeacherCount || otherTeacherCount.count === 0) {
      orphanStudentIds.push(s.student_id);
    }
  }
  if (orphanStudentIds.length > 0 && !deleteOrphanStudents) {
    return c.json(
      {
        error: "ORPHAN_STUDENTS_EXIST",
        message: `\u5B58\u5728 ${orphanStudentIds.length} \u540D\u4EC5\u5F52\u5C5E\u8BE5\u6559\u5E08\u7684\u5B66\u751F\uFF0C\u8BF7\u786E\u8BA4\u662F\u5426\u8FDE\u5E26\u6E05\u7406\u8D26\u53F7`,
        orphanCount: orphanStudentIds.length
      },
      409
    );
  }
  if (deleteOrphanStudents && orphanStudentIds.length > 0) {
    for (const orphanId of orphanStudentIds) {
      await db.prepare("DELETE FROM users WHERE id = ?").bind(orphanId).run();
    }
  }
  await db.prepare("DELETE FROM users WHERE id = ?").bind(teacherId).run();
  return c.json({ success: true, message: "\u6559\u5E08\u53CA\u5173\u8054\u8D44\u6E90\u5DF2\u6210\u529F\u5220\u9664" });
});
adminRouter.get("/teachers/:teacherId/students", async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param("teacherId");
  const boundStudents = await db.prepare(
    `SELECT u.id, u.username, u.display_name as displayName, ts.bound_at as boundAt,
              (SELECT COUNT(*) FROM teacher_students ts2 WHERE ts2.student_id = u.id) as totalTeachersCount
       FROM users u
       JOIN teacher_students ts ON u.id = ts.student_id
       WHERE ts.teacher_id = ?
       ORDER BY ts.bound_at DESC`
  ).bind(teacherId).all();
  const availableStudents = await db.prepare(
    `SELECT u.id, u.username, u.display_name as displayName,
              (SELECT COUNT(*) FROM teacher_students ts WHERE ts.student_id = u.id) as totalTeachersCount
       FROM users u
       WHERE u.role = 'student'
         AND u.id NOT IN (SELECT student_id FROM teacher_students WHERE teacher_id = ?)
       ORDER BY u.created_at DESC`
  ).bind(teacherId).all();
  return c.json({
    boundStudents: boundStudents.results,
    availableStudents: availableStudents.results
  });
});
adminRouter.post("/teachers/:teacherId/students", async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param("teacherId");
  const body = await c.req.json().catch(() => ({}));
  const { mode, username, displayName, studentIds } = body;
  const teacher = await db.prepare("SELECT id FROM users WHERE id = ? AND role = 'teacher'").bind(teacherId).first();
  if (!teacher) {
    return c.json({ error: "TEACHER_NOT_FOUND", message: "\u6307\u5B9A\u6559\u5E08\u4E0D\u5B58\u5728" }, 404);
  }
  const now = Date.now();
  if (mode === "create") {
    if (!username) {
      return c.json({ error: "BAD_REQUEST", message: "\u7528\u6237\u540D\u4E0D\u80FD\u4E3A\u7A7A" }, 400);
    }
    const trimmedUsername = username.trim();
    const existing = await db.prepare("SELECT id FROM users WHERE username = ?").bind(trimmedUsername).first();
    if (existing) {
      return c.json({ error: "USERNAME_EXISTS", message: "\u8BE5\u7528\u6237\u540D\u5DF2\u5B58\u5728" }, 409);
    }
    const studentId = crypto.randomUUID();
    const passwordHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
    await db.prepare(
      "INSERT INTO users (id, username, display_name, password_hash, role, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"
    ).bind(
      studentId,
      trimmedUsername,
      displayName?.trim() || trimmedUsername,
      passwordHash,
      "student",
      now,
      now
    ).run();
    await db.prepare("INSERT INTO teacher_students (teacher_id, student_id, bound_at) VALUES (?, ?, ?)").bind(teacherId, studentId, now).run();
    return c.json({
      student: {
        id: studentId,
        username: trimmedUsername,
        displayName: displayName?.trim() || trimmedUsername,
        boundAt: now,
        totalTeachersCount: 1
      }
    });
  } else if (mode === "bind_existing") {
    if (!Array.isArray(studentIds) || studentIds.length === 0) {
      return c.json({ error: "BAD_REQUEST", message: "\u8BF7\u9009\u62E9\u8981\u7ED1\u5B9A\u7684\u5B66\u751F" }, 400);
    }
    for (const sid of studentIds) {
      await db.prepare("INSERT OR IGNORE INTO teacher_students (teacher_id, student_id, bound_at) VALUES (?, ?, ?)").bind(teacherId, sid, now).run();
    }
    return c.json({ success: true, boundCount: studentIds.length });
  }
  return c.json({ error: "BAD_REQUEST", message: "\u65E0\u6548\u7684 mode \u64CD\u4F5C" }, 400);
});
adminRouter.delete("/teachers/:teacherId/students/:studentId", async (c) => {
  const db = c.env.DB;
  const teacherId = c.req.param("teacherId");
  const studentId = c.req.param("studentId");
  const action = c.req.query("action") || "unbind";
  if (action === "delete_account") {
    await db.prepare("DELETE FROM users WHERE id = ?").bind(studentId).run();
    return c.json({ success: true, message: "\u5B66\u751F\u8D26\u53F7\u5DF2\u5F7B\u5E95\u5220\u9664" });
  }
  const otherTeachers = await db.prepare("SELECT COUNT(*) as count FROM teacher_students WHERE student_id = ? AND teacher_id != ?").bind(studentId, teacherId).first();
  const isOrphan = !otherTeachers || otherTeachers.count === 0;
  const force = c.req.query("force") === "true";
  if (isOrphan && !force) {
    return c.json(
      {
        error: "LAST_TEACHER_WARNING",
        message: "\u8BE5\u5B66\u751F\u5F53\u524D\u4EC5\u5F52\u5C5E\u8FD9\u4E00\u540D\u6559\u5E08\uFF0C\u79FB\u51FA\u540E\u5C06\u5931\u53BB\u6240\u6709\u6559\u5E08\u5F52\u5C5E\u3002\u8BF7\u786E\u8BA4\u662F\u5426\u5F3A\u5236\u79FB\u51FA\u5E76\u5F7B\u5E95\u5220\u9664\u8D26\u53F7\uFF1F"
      },
      409
    );
  }
  if (isOrphan && force) {
    await db.prepare("DELETE FROM users WHERE id = ?").bind(studentId).run();
    return c.json({ success: true, message: "\u5B66\u751F\u552F\u4E00\u5F52\u5C5E\u5DF2\u89E3\u9664\uFF0C\u8D26\u53F7\u5DF2\u5F7B\u5E95\u6E05\u7406" });
  }
  await db.prepare("DELETE FROM teacher_students WHERE teacher_id = ? AND student_id = ?").bind(teacherId, studentId).run();
  return c.json({ success: true, message: "\u5DF2\u4ECE\u5F53\u524D\u6559\u5E08\u540D\u4E0B\u79FB\u51FA" });
});
adminRouter.post("/users/:userId/reset-password", async (c) => {
  const db = c.env.DB;
  const userId = c.req.param("userId");
  const user = await db.prepare("SELECT id FROM users WHERE id = ?").bind(userId).first();
  if (!user) {
    return c.json({ error: "NOT_FOUND", message: "\u7528\u6237\u4E0D\u5B58\u5728" }, 404);
  }
  const newHash = await hashPassword(DEFAULT_NEW_USER_PASSWORD);
  const now = Date.now();
  await db.prepare("UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?").bind(newHash, now, userId).run();
  return c.json({ success: true, message: `\u5BC6\u7801\u5DF2\u91CD\u7F6E\u4E3A\u9ED8\u8BA4\u5BC6\u7801: ${DEFAULT_NEW_USER_PASSWORD}` });
});

// shared/inflections.ts
function normalizeText(text) {
  return text.trim().toLowerCase();
}

// functions/api/services/enricher.ts
var COMMON_HOMOPHONE_MAP = {
  right: ["write", "rite"],
  write: ["right", "rite"],
  meat: ["meet"],
  meet: ["meat"],
  sea: ["see"],
  see: ["sea"],
  sun: ["son"],
  son: ["sun"],
  hear: ["here"],
  here: ["hear"],
  buy: ["by", "bye"],
  by: ["buy", "bye"],
  hour: ["our"],
  our: ["hour"],
  flower: ["flour"],
  flour: ["flower"],
  piece: ["peace"],
  peace: ["piece"],
  wait: ["weight"],
  weight: ["wait"],
  tail: ["tale"],
  tale: ["tail"],
  road: ["rode", "rowed"],
  rode: ["road"],
  pair: ["pear"],
  pear: ["pair"],
  stare: ["stair"],
  stair: ["stare"]
};
function heuristicSyllableSplit(word) {
  if (word.includes(" ") || word.includes("-") || word.length <= 4) {
    return word;
  }
  return word.replace(/(in)(for)(ma)(tion)/gi, "$1\xB7$2\xB7$3\xB7$4").replace(/(com)(pu)(ter)/gi, "$1\xB7$2\xB7$3").replace(/(beau)(ti)(ful)/gi, "$1\xB7$2\xB7$3").replace(/([^aeiouy]{1,2})([aeiouy]+)([^aeiouy]+)([aeiouy]+)/gi, (match2, p1, p2, p3, p4) => {
    if (p3.length >= 2) {
      return `${p1}${p2}${p3.slice(0, 1)}\xB7${p3.slice(1)}${p4}`;
    }
    return `${p1}${p2}\xB7${p3}${p4}`;
  });
}
function deriveInflections(word) {
  const norm = normalizeText(word);
  if (norm.includes(" ")) {
    const parts = norm.split(" ");
    const first = parts[0];
    const rest = parts.slice(1).join(" ");
    const firstInf = deriveInflections(first);
    return {
      thirdPerson: firstInf.thirdPerson ? `${firstInf.thirdPerson} ${rest}` : void 0,
      presentParticiple: firstInf.presentParticiple ? `${firstInf.presentParticiple} ${rest}` : void 0,
      pastTense: firstInf.pastTense ? `${firstInf.pastTense} ${rest}` : void 0,
      pastParticiple: firstInf.pastParticiple ? `${firstInf.pastParticiple} ${rest}` : void 0
    };
  }
  let plural;
  let thirdPerson;
  let presentParticiple;
  let pastTense;
  let pastParticiple;
  if (norm.endsWith("y") && !/[aeiou]y$/.test(norm)) {
    plural = norm.slice(0, -1) + "ies";
    thirdPerson = plural;
  } else if (/(s|sh|ch|x|z)$/.test(norm)) {
    plural = norm + "es";
    thirdPerson = plural;
  } else {
    plural = norm + "s";
    thirdPerson = plural;
  }
  if (norm.endsWith("ie")) {
    presentParticiple = norm.slice(0, -2) + "ying";
  } else if (norm.endsWith("e") && !/(ee|oe|ye)$/.test(norm)) {
    presentParticiple = norm.slice(0, -1) + "ing";
  } else if (/[^aeiou][aeiou][^aeiouwxy]$/.test(norm) && norm.length <= 4) {
    presentParticiple = norm + norm.slice(-1) + "ing";
  } else {
    presentParticiple = norm + "ing";
  }
  if (norm.endsWith("e")) {
    pastTense = norm + "d";
    pastParticiple = norm + "d";
  } else if (norm.endsWith("y") && !/[aeiou]y$/.test(norm)) {
    pastTense = norm.slice(0, -1) + "ied";
    pastParticiple = pastTense;
  } else if (/[^aeiou][aeiou][^aeiouwxy]$/.test(norm) && norm.length <= 4) {
    pastTense = norm + norm.slice(-1) + "ed";
    pastParticiple = pastTense;
  } else {
    pastTense = norm + "ed";
    pastParticiple = pastTense;
  }
  return {
    plural,
    thirdPerson,
    presentParticiple,
    pastTense,
    pastParticiple
  };
}
async function enrichWord(rawText, aiBinding) {
  const text = rawText.trim();
  const normalizedText = normalizeText(text);
  let syllables = heuristicSyllableSplit(text);
  let phoneticUs = "";
  let phoneticUk = "";
  let audioUsUrl = `/api/speech/tts?text=${encodeURIComponent(text)}&accent=us`;
  let audioUkUrl = `/api/speech/tts?text=${encodeURIComponent(text)}&accent=uk`;
  let meanings = [];
  let inflections = deriveInflections(text);
  let homophones = COMMON_HOMOPHONE_MAP[normalizedText] || [];
  let examples = [];
  let tags = [];
  try {
    const dictResp = await fetch(`https://api.dictionaryapi.dev/api/v2/entries/en/${encodeURIComponent(normalizedText)}`, {
      signal: AbortSignal.timeout(2500)
    });
    if (dictResp.ok) {
      const data = await dictResp.json();
      if (Array.isArray(data) && data[0]) {
        const item = data[0];
        if (item.phonetics && Array.isArray(item.phonetics)) {
          for (const p of item.phonetics) {
            if (p.text && !phoneticUs) phoneticUs = p.text;
            if (p.audio && !audioUsUrl.startsWith("http")) {
              audioUsUrl = p.audio;
            }
          }
        }
        if (!phoneticUs && item.phonetic) {
          phoneticUs = item.phonetic;
        }
        if (item.meanings && Array.isArray(item.meanings)) {
          for (const m of item.meanings) {
            const pos = m.partOfSpeech ? `${m.partOfSpeech}.` : "";
            if (m.definitions && m.definitions[0]) {
              const def = m.definitions[0];
              meanings.push({
                pos: pos || "n.",
                zh: def.definition || ""
              });
              if (def.example && examples.length < 2) {
                examples.push({
                  en: def.example,
                  zh: "\u8BF7\u9605\u8BFB\u7406\u89E3\u8BE5\u4F8B\u53E5",
                  targetInSentence: text
                });
              }
            }
          }
        }
      }
    }
  } catch {
  }
  if (aiBinding && typeof aiBinding.run === "function") {
    try {
      const prompt = `You are an expert English lexicographer. Provide structured vocabulary JSON for the word or phrase: "${text}".
Output valid JSON only with keys:
- syllables: string with syllable dots like "in\xB7for\xB7ma\xB7tion"
- phoneticUs: string like "/.../"
- meanings: array of { pos: string, zh: string } with accurate simplified Chinese translation
- inflections: { plural, thirdPerson, presentParticiple, pastTense, pastParticiple, comparative, superlative }
- homophones: array of strings of valid homophones (e.g. right -> ["write"])
- examples: array of { en: string, zh: string, targetInSentence: string } (targetInSentence is the exact word form used in the sentence).
DO NOT return any markdown wrapping.`;
      const aiResp = await aiBinding.run("@cf/meta/llama-3.1-8b-instruct", {
        messages: [{ role: "user", content: prompt }],
        max_tokens: 800
      });
      if (aiResp && aiResp.response) {
        const cleaned = aiResp.response.replace(/```json/g, "").replace(/```/g, "").trim();
        const parsed = JSON.parse(cleaned);
        if (parsed.syllables) syllables = parsed.syllables;
        if (parsed.phoneticUs) phoneticUs = parsed.phoneticUs;
        if (Array.isArray(parsed.meanings) && parsed.meanings.length > 0) meanings = parsed.meanings;
        if (parsed.inflections) inflections = { ...inflections, ...parsed.inflections };
        if (Array.isArray(parsed.homophones)) homophones = parsed.homophones;
        if (Array.isArray(parsed.examples) && parsed.examples.length > 0) examples = parsed.examples;
      }
    } catch {
    }
  }
  if (meanings.length === 0) {
    meanings = [{ pos: text.includes(" ") ? "phr." : "n./v.", zh: "\uFF08\u8BF7\u7F16\u8F91\u8F93\u5165\u4E2D\u6587\u91CA\u4E49\uFF09" }];
  }
  if (examples.length === 0) {
    examples = [
      {
        en: `This is an example sentence for ${text}.`,
        zh: `\u8FD9\u662F\u5173\u4E8E ${text} \u7684\u4F8B\u53E5\u3002`,
        targetInSentence: text
      }
    ];
  }
  return {
    text,
    normalizedText,
    syllables,
    phoneticUs: phoneticUs || `/${normalizedText}/`,
    phoneticUk: phoneticUk || phoneticUs || `/${normalizedText}/`,
    audioUsUrl,
    audioUkUrl,
    meanings,
    inflections,
    homophones,
    examples,
    tags
  };
}

// shared/ebbinghaus.ts
function getTodayDateStr(date = /* @__PURE__ */ new Date()) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}`;
}

// functions/api/routes/teacher.ts
var teacherRouter = new Hono3();
teacherRouter.use("*", authMiddleware, requireRole("teacher"));
teacherRouter.post("/words/enrich", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { text } = body;
  if (!text || typeof text !== "string") {
    return c.json({ error: "BAD_REQUEST", message: "\u8BF7\u8F93\u5165\u8981\u8865\u5168\u7684\u5355\u8BCD\u6216\u8BCD\u7EC4" }, 400);
  }
  const enriched = await enrichWord(text, c.env.AI);
  return c.json({ word: enriched });
});
teacherRouter.get("/words", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const query = c.req.query("q")?.toLowerCase();
  const tag2 = c.req.query("tag");
  let sql = "SELECT * FROM teacher_words WHERE teacher_id = ?";
  const params = [user.id];
  if (query) {
    sql += " AND (normalized_text LIKE ? OR meanings_json LIKE ?)";
    params.push(`%${query}%`, `%${query}%`);
  }
  if (tag2) {
    sql += " AND tags_json LIKE ?";
    params.push(`%"${tag2}"%`);
  }
  sql += " ORDER BY created_at DESC";
  const rows = await db.prepare(sql).bind(...params).all();
  const words = rows.results.map((r) => ({
    id: r.id,
    teacherId: r.teacher_id,
    text: r.text,
    normalizedText: r.normalized_text,
    syllables: r.syllables,
    phoneticUs: r.phonetic_us,
    phoneticUk: r.phonetic_uk,
    audioUsUrl: r.audio_us_url,
    audioUkUrl: r.audio_uk_url,
    meanings: JSON.parse(r.meanings_json || "[]"),
    inflections: JSON.parse(r.inflections_json || "{}"),
    homophones: JSON.parse(r.homophones_json || "[]"),
    examples: JSON.parse(r.examples_json || "[]"),
    tags: JSON.parse(r.tags_json || "[]"),
    createdAt: r.created_at,
    updatedAt: r.updated_at
  }));
  return c.json({ words });
});
teacherRouter.post("/words", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const body = await c.req.json().catch(() => ({}));
  const {
    text,
    syllables,
    phoneticUs,
    phoneticUk,
    audioUsUrl,
    audioUkUrl,
    meanings,
    inflections,
    homophones,
    examples,
    tags
  } = body;
  if (!text) {
    return c.json({ error: "BAD_REQUEST", message: "\u5355\u8BCD\u5185\u5BB9\u4E0D\u80FD\u4E3A\u7A7A" }, 400);
  }
  const normalizedText = normalizeText(text);
  const existing = await db.prepare("SELECT id, text FROM teacher_words WHERE teacher_id = ? AND normalized_text = ?").bind(user.id, normalizedText).first();
  if (existing) {
    return c.json(
      {
        error: "DUPLICATE_WORD",
        message: `\u8BCD\u6761 "${existing.text}" \u5DF2\u5B58\u5728\u4E8E\u60A8\u7684\u8BCD\u5E93\u4E2D\uFF0C\u65E0\u9700\u91CD\u590D\u5F55\u5165`,
        existingId: existing.id
      },
      409
    );
  }
  const wordId = crypto.randomUUID();
  const now = Date.now();
  await db.prepare(
    `INSERT INTO teacher_words (
        id, teacher_id, text, normalized_text, syllables,
        phonetic_us, phonetic_uk, audio_us_url, audio_uk_url,
        meanings_json, inflections_json, homophones_json, examples_json, tags_json,
        created_at, updated_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
  ).bind(
    wordId,
    user.id,
    text.trim(),
    normalizedText,
    syllables || text.trim(),
    phoneticUs || "",
    phoneticUk || "",
    audioUsUrl || "",
    audioUkUrl || "",
    JSON.stringify(meanings || []),
    JSON.stringify(inflections || {}),
    JSON.stringify(homophones || []),
    JSON.stringify(examples || []),
    JSON.stringify(tags || []),
    now,
    now
  ).run();
  return c.json({
    word: {
      id: wordId,
      teacherId: user.id,
      text: text.trim(),
      normalizedText,
      syllables: syllables || text.trim(),
      phoneticUs: phoneticUs || "",
      phoneticUk: phoneticUk || "",
      audioUsUrl: audioUsUrl || "",
      audioUkUrl: audioUkUrl || "",
      meanings: meanings || [],
      inflections: inflections || {},
      homophones: homophones || [],
      examples: examples || [],
      tags: tags || [],
      createdAt: now,
      updatedAt: now
    }
  });
});
teacherRouter.put("/words/:wordId", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const wordId = c.req.param("wordId");
  const body = await c.req.json().catch(() => ({}));
  const existing = await db.prepare("SELECT id FROM teacher_words WHERE id = ? AND teacher_id = ?").bind(wordId, user.id).first();
  if (!existing) {
    return c.json({ error: "NOT_FOUND", message: "\u8BCD\u6761\u4E0D\u5B58\u5728\u6216\u65E0\u6743\u7F16\u8F91" }, 404);
  }
  const now = Date.now();
  await db.prepare(
    `UPDATE teacher_words SET
        text = ?, syllables = ?, phonetic_us = ?, phonetic_uk = ?,
        audio_us_url = ?, audio_uk_url = ?, meanings_json = ?,
        inflections_json = ?, homophones_json = ?, examples_json = ?,
        tags_json = ?, updated_at = ?
       WHERE id = ? AND teacher_id = ?`
  ).bind(
    body.text?.trim(),
    body.syllables || body.text?.trim(),
    body.phoneticUs || "",
    body.phoneticUk || "",
    body.audioUsUrl || "",
    body.audioUkUrl || "",
    JSON.stringify(body.meanings || []),
    JSON.stringify(body.inflections || {}),
    JSON.stringify(body.homophones || []),
    JSON.stringify(body.examples || []),
    JSON.stringify(body.tags || []),
    now,
    wordId,
    user.id
  ).run();
  return c.json({ success: true, message: "\u8BCD\u6761\u66F4\u65B0\u6210\u529F" });
});
teacherRouter.delete("/words/:wordId", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const wordId = c.req.param("wordId");
  await db.prepare("DELETE FROM teacher_words WHERE id = ? AND teacher_id = ?").bind(wordId, user.id).run();
  return c.json({ success: true, message: "\u8BCD\u6761\u5DF2\u5220\u9664" });
});
teacherRouter.get("/analytics/students", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const today = getTodayDateStr();
  const students = await db.prepare(
    `SELECT u.id as studentId, u.username, u.display_name as displayName
       FROM users u
       JOIN teacher_students ts ON u.id = ts.student_id
       WHERE ts.teacher_id = ?
       ORDER BY ts.bound_at DESC`
  ).bind(user.id).all();
  const totalTeacherWords = await db.prepare("SELECT COUNT(*) as count FROM teacher_words WHERE teacher_id = ?").bind(user.id).first();
  const totalWords = totalTeacherWords?.count || 0;
  const summaries = [];
  for (const s of students.results) {
    const stats = await db.prepare(
      `SELECT
          SUM(CASE WHEN p.status = 'reviewing' THEN 1 ELSE 0 END) as reviewingCount,
          SUM(CASE WHEN p.status = 'mastered' THEN 1 ELSE 0 END) as masteredCount,
          SUM(CASE WHEN p.status = 'reviewing' AND p.next_review_date <= ? THEN 1 ELSE 0 END) as todayDueCount,
          SUM(CASE WHEN p.last_reviewed_date = ? THEN 1 ELSE 0 END) as todayCompletedCount,
          MAX(p.last_studied_at) as lastActiveAt
         FROM teacher_words tw
         LEFT JOIN student_word_progress p
           ON p.student_id = ? AND p.normalized_text = tw.normalized_text
         WHERE tw.teacher_id = ?`
    ).bind(today, today, s.studentId, user.id).first();
    const reviewing = stats?.reviewingCount || 0;
    const mastered = stats?.masteredCount || 0;
    const unlearned = Math.max(0, totalWords - reviewing - mastered);
    summaries.push({
      studentId: s.studentId,
      username: s.username,
      displayName: s.displayName,
      totalWords,
      unlearnedCount: unlearned,
      reviewingCount: reviewing,
      masteredCount: mastered,
      todayDueReviewCount: stats?.todayDueCount || 0,
      todayCompletedReviewCount: stats?.todayCompletedCount || 0,
      lastActiveAt: stats?.lastActiveAt || null
    });
  }
  return c.json({ summaries });
});
teacherRouter.get("/analytics/students/:studentId/weak-words", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const studentId = c.req.param("studentId");
  const isBound = await db.prepare("SELECT 1 FROM teacher_students WHERE teacher_id = ? AND student_id = ?").bind(user.id, studentId).first();
  if (!isBound) {
    return c.json({ error: "FORBIDDEN", message: "\u65E0\u6743\u67E5\u770B\u975E\u540D\u4E0B\u5B66\u751F\u5B66\u60C5" }, 403);
  }
  const rows = await db.prepare(
    `SELECT tw.text, tw.normalized_text as normalizedText, tw.meanings_json as meaningsJson,
              p.error_count as errorCount, p.stage_reset_count as stageResetCount,
              p.last_error_step as lastErrorStep, p.ebbinghaus_stage as currentStage,
              p.next_review_date as nextReviewDate
       FROM student_word_progress p
       JOIN teacher_words tw ON tw.normalized_text = p.normalized_text
       WHERE p.student_id = ? AND tw.teacher_id = ? AND p.error_count > 0
       ORDER BY p.error_count DESC, p.stage_reset_count DESC
       LIMIT 50`
  ).bind(studentId, user.id).all();
  const weakWords = rows.results.map((r) => ({
    text: r.text,
    normalizedText: r.normalizedText,
    meanings: JSON.parse(r.meaningsJson || "[]"),
    errorCount: r.errorCount,
    stageResetCount: r.stageResetCount,
    lastErrorStep: r.lastErrorStep,
    currentStage: r.currentStage,
    nextReviewDate: r.nextReviewDate
  }));
  return c.json({ weakWords });
});

// shared/phonetics.ts
function levenshteinDistance(s1, s2) {
  const m = s1.length;
  const n = s2.length;
  const dp = Array.from({ length: m + 1 }, () => Array(n + 1).fill(0));
  for (let i = 0; i <= m; i++) dp[i][0] = i;
  for (let j = 0; j <= n; j++) dp[0][j] = j;
  for (let i = 1; i <= m; i++) {
    for (let j = 1; j <= n; j++) {
      if (s1[i - 1] === s2[j - 1]) {
        dp[i][j] = dp[i - 1][j - 1];
      } else {
        dp[i][j] = 1 + Math.min(dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]);
      }
    }
  }
  return dp[m][n];
}
function evaluatePronunciationSimilarity(targetText, recognizedText) {
  const cleanTarget = normalizeText(targetText).replace(/[^a-z0-9\s]/g, "");
  const cleanRecognized = normalizeText(recognizedText).replace(/[^a-z0-9\s]/g, "");
  if (!cleanRecognized) {
    return { score: 0, isAccurate: false, cleanTarget, cleanRecognized };
  }
  if (cleanTarget === cleanRecognized) {
    return { score: 100, isAccurate: true, cleanTarget, cleanRecognized };
  }
  const maxLen = Math.max(cleanTarget.length, cleanRecognized.length);
  if (maxLen === 0) {
    return { score: 100, isAccurate: true, cleanTarget, cleanRecognized };
  }
  const distance = levenshteinDistance(cleanTarget, cleanRecognized);
  const ratio = Math.max(0, 1 - distance / maxLen);
  const score = Math.round(ratio * 100);
  return {
    score,
    isAccurate: score >= 80,
    cleanTarget,
    cleanRecognized
  };
}

// functions/api/routes/speech.ts
var speechRouter = new Hono3();
speechRouter.get("/tts", async (c) => {
  const text = c.req.query("text");
  const accent = c.req.query("accent") || "us";
  if (!text) {
    return c.text("Text required", 400);
  }
  const voiceType = accent === "uk" ? "1" : "2";
  const upstreamUrl = `https://dict.youdao.com/dictvoice?audio=${encodeURIComponent(text)}&type=${voiceType}`;
  try {
    const upstreamResp = await fetch(upstreamUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
      }
    });
    if (!upstreamResp.ok) {
      return c.text("Audio fetch failed", 502);
    }
    const audioBytes = await upstreamResp.arrayBuffer();
    return new Response(audioBytes, {
      headers: {
        "Content-Type": "audio/mpeg",
        "Cache-Control": "public, max-age=2592000, immutable",
        // 30天强缓存
        "Access-Control-Allow-Origin": "*"
      }
    });
  } catch {
    return c.text("Audio proxy error", 500);
  }
});
speechRouter.post("/evaluate", async (c) => {
  const body = await c.req.json().catch(() => ({}));
  const { targetText, recognizedTextFallback, audioBase64 } = body;
  if (!targetText) {
    return c.json({ error: "BAD_REQUEST", message: "targetText is required" }, 400);
  }
  let recognized = recognizedTextFallback || "";
  if (audioBase64 && c.env.AI && typeof c.env.AI.run === "function") {
    try {
      const binaryString = atob(audioBase64);
      const bytes = new Uint8Array(binaryString.length);
      for (let i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
      }
      const whisperResp = await c.env.AI.run("@cf/openai/whisper", {
        audio: [...bytes]
      });
      if (whisperResp && whisperResp.text) {
        recognized = whisperResp.text;
      }
    } catch {
    }
  }
  const result = evaluatePronunciationSimilarity(targetText, recognized);
  return c.json({
    recognizedText: recognized,
    score: result.score,
    isAccurate: result.isAccurate,
    feedback: result.isAccurate ? "\u53D1\u97F3\u975E\u5E38\u6807\u51C6\uFF01" : "\u8BFB\u97F3\u4E0E\u76EE\u6807\u7565\u6709\u504F\u5DEE\uFF0C\u8BF7\u91CD\u542C\u6807\u51C6\u97F3\u5BF9\u6BD4\u540E\u518D\u8BD5"
  });
});

// functions/api/routes/student.ts
var studentRouter = new Hono3();
studentRouter.use("*", authMiddleware, requireRole("student"));
studentRouter.get("/sync/pull", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const teachers = await db.prepare(
    `SELECT u.id, u.display_name as displayName
       FROM users u
       JOIN teacher_students ts ON u.id = ts.teacher_id
       WHERE ts.student_id = ?`
  ).bind(user.id).all();
  const teacherIds = teachers.results.map((t) => t.id);
  if (teacherIds.length === 0) {
    return c.json({
      teachers: [],
      words: [],
      progress: [],
      serverTime: Date.now()
    });
  }
  const placeholders = teacherIds.map(() => "?").join(",");
  const wordRows = await db.prepare(
    `SELECT tw.*, u.display_name as teacherName
       FROM teacher_words tw
       JOIN users u ON u.id = tw.teacher_id
       WHERE tw.teacher_id IN (${placeholders})
       ORDER BY tw.created_at ASC`
  ).bind(...teacherIds).all();
  const wordMap = /* @__PURE__ */ new Map();
  for (const r of wordRows.results) {
    const norm = r.normalized_text;
    const examples = JSON.parse(r.examples_json || "[]");
    if (!wordMap.has(norm)) {
      wordMap.set(norm, {
        id: r.id,
        teacherId: r.teacher_id,
        teacherName: r.teacherName,
        text: r.text,
        normalizedText: norm,
        syllables: r.syllables,
        phoneticUs: r.phonetic_us,
        phoneticUk: r.phonetic_uk,
        audioUsUrl: r.audio_us_url,
        audioUkUrl: r.audio_uk_url,
        meanings: JSON.parse(r.meanings_json || "[]"),
        inflections: JSON.parse(r.inflections_json || "{}"),
        homophones: JSON.parse(r.homophones_json || "[]"),
        examples,
        tags: JSON.parse(r.tags_json || "[]"),
        createdAt: r.created_at,
        updatedAt: r.updated_at
      });
    } else {
      const existing = wordMap.get(norm);
      for (const ex of examples) {
        if (!existing.examples.some((e) => e.en === ex.en)) {
          existing.examples.push(ex);
        }
      }
    }
  }
  const progressRows = await db.prepare("SELECT * FROM student_word_progress WHERE student_id = ?").bind(user.id).all();
  const progressList = progressRows.results.map((r) => ({
    studentId: r.student_id,
    normalizedText: r.normalized_text,
    status: r.status,
    ebbinghausStage: r.ebbinghaus_stage,
    nextReviewDate: r.next_review_date,
    lastStudiedAt: r.last_studied_at,
    lastReviewedDate: r.last_reviewed_date,
    studyCheckpoint: r.study_checkpoint_json ? JSON.parse(r.study_checkpoint_json) : null,
    totalReviewCount: r.total_review_count,
    errorCount: r.error_count,
    stageResetCount: r.stage_reset_count,
    lastErrorStep: r.last_error_step,
    updatedAt: r.updated_at
  }));
  return c.json({
    teachers: teachers.results,
    words: Array.from(wordMap.values()),
    progress: progressList,
    serverTime: Date.now()
  });
});
studentRouter.post("/sync/push", async (c) => {
  const db = c.env.DB;
  const user = c.get("user");
  const body = await c.req.json().catch(() => ({}));
  const updates = body.updates || [];
  if (!Array.isArray(updates) || updates.length === 0) {
    return c.json({ success: true, processedCount: 0 });
  }
  const now = Date.now();
  for (const item of updates) {
    if (!item.normalizedText) continue;
    await db.prepare(
      `INSERT INTO student_word_progress (
          student_id, normalized_text, status, ebbinghaus_stage, next_review_date,
          last_studied_at, last_reviewed_date, study_checkpoint_json,
          total_review_count, error_count, stage_reset_count, last_error_step, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(student_id, normalized_text) DO UPDATE SET
          status = excluded.status,
          ebbinghaus_stage = excluded.ebbinghaus_stage,
          next_review_date = excluded.next_review_date,
          last_studied_at = excluded.last_studied_at,
          last_reviewed_date = excluded.last_reviewed_date,
          study_checkpoint_json = excluded.study_checkpoint_json,
          total_review_count = excluded.total_review_count,
          error_count = excluded.error_count,
          stage_reset_count = excluded.stage_reset_count,
          last_error_step = excluded.last_error_step,
          updated_at = excluded.updated_at
        WHERE excluded.updated_at >= student_word_progress.updated_at`
    ).bind(
      user.id,
      item.normalizedText,
      item.status || "unlearned",
      item.ebbinghausStage ?? 0,
      item.nextReviewDate || null,
      item.lastStudiedAt || null,
      item.lastReviewedDate || null,
      item.studyCheckpoint ? JSON.stringify(item.studyCheckpoint) : null,
      item.totalReviewCount ?? 0,
      item.errorCount ?? 0,
      item.stageResetCount ?? 0,
      item.lastErrorStep || null,
      item.updatedAt || now
    ).run();
  }
  return c.json({ success: true, processedCount: updates.length });
});

// functions/api/[[route]].ts
var app = new Hono3().basePath("/api");
app.use("*", async (c, next) => {
  await next();
  c.res.headers.set("Access-Control-Allow-Origin", "*");
  c.res.headers.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  c.res.headers.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
});
app.options("*", (c) => c.body(null, 204));
app.route("/auth", authRouter);
app.route("/admin", adminRouter);
app.route("/teacher", teacherRouter);
app.route("/speech", speechRouter);
app.route("/student", studentRouter);
var onRequest = handle(app);
var route_default = app;

// src/workerEntry.ts
import { ASSETS } from "./static_assets.js";
var workerEntry_default = {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const pathname = decodeURIComponent(url.pathname);
    if (pathname.startsWith("/api")) {
      return route_default.fetch(request, env, ctx);
    }
    if (request.method === "OPTIONS") {
      return new Response(null, {
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization",
          "Access-Control-Max-Age": "86400"
        }
      });
    }
    const normalizedPath = "/" + pathname.replace(/^\/(?:static\/)?/, "");
    const assetMap = ASSETS || {};
    let asset = assetMap[pathname] || assetMap[normalizedPath];
    if (!asset && pathname.endsWith("/")) {
      asset = assetMap[pathname + "index.html"];
    }
    if (!asset && !pathname.slice(1).includes(".")) {
      asset = assetMap["/index.html"] || assetMap["/"];
    }
    if (asset) {
      let cacheControl = "public, max-age=3600";
      if (pathname === "/" || pathname.endsWith(".html") || pathname.endsWith("sw.js")) {
        cacheControl = "no-cache, no-store, must-revalidate";
      } else if (pathname.startsWith("/assets/")) {
        cacheControl = "public, max-age=31536000, immutable";
      }
      if (asset.base64) {
        const binary = Uint8Array.from(atob(asset.base64), (c) => c.charCodeAt(0));
        return new Response(binary, {
          headers: {
            "Content-Type": asset.contentType,
            "Cache-Control": cacheControl,
            "Access-Control-Allow-Origin": "*"
          }
        });
      }
      return new Response(asset.content, {
        headers: {
          "Content-Type": asset.contentType,
          "Cache-Control": cacheControl,
          "Access-Control-Allow-Origin": "*"
        }
      });
    }
    const fallback = assetMap["/index.html"] || assetMap["/"];
    if (fallback) {
      return new Response(fallback.content, {
        headers: {
          "Content-Type": "text/html; charset=utf-8",
          "Cache-Control": "no-cache, no-store, must-revalidate"
        }
      });
    }
    return new Response("404 Not Found", { status: 404 });
  }
};
export {
  workerEntry_default as default
};
