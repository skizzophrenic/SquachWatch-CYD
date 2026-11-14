// SquachWatches in the Wild -- the flasher's install counter.
//
// A Cloudflare Worker at count.squachwatch.com with one D1 database bound as
// DB. The flasher page sends one POST /hit when an install finishes, naming
// the board; GET / returns the running total and the per-board tally for the
// page to show.
//
// It keeps numbers and nothing else: no address, no browser, no time, no
// cookie. The board name is checked against the list below, so the tally
// only ever holds boards the flasher actually offers.
//
// The total starts at BASE, a guess at the installs before counting began
// (2026-10); the per-board tally starts at zero.
//
// Why D1 and not KV (2026-10-20): the free plan allows 1,000 KV writes a day
// and every install cost two, so the counter warned at half its quota after
// a day and a half. D1 allows 100,000 rows written a day, and an install is
// one row: an upsert that adds one in the database, so two installs landing
// at the same instant can no longer lose a count either.
//
// The move carries the KV numbers over by itself. While the old KV namespace
// is still bound as COUNTS, the first request to find the table empty copies
// the per-board tally across, plus a hidden "_offset" row for any difference
// between KV's total and the sum of its boards (the two could drift by one).
// Once the numbers show on the page the COUNTS binding can be removed.

const BASE = 5000;
const BOARDS = new Set([
  "cyd", "cyd-fast", "cyd-ili9341", "cyd-ili9341-fast",
  "freenove32", "cyd32c", "cyd35-fast", "cyd35c-fast",
  "awok", "rlphantom-r",
  "freenove-s3", "freenove-s3-40mhz", "freenove-s3-35", "crowpanel7", "tdeck-plus",
  "nm-cyd-c5", "marauder-v8",
  "sticks3", "cardputer-adv",
  "twatch-s3",
  "tab5",
]);
const ORIGINS = new Set(["https://squachwatch.com", "https://www.squachwatch.com"]);

// The page asks for the tally on every visit. One copy is kept at the edge
// for this long, so a busy minute costs one database read, not hundreds.
const CACHE_SECONDS = 60;
const CACHE_KEY = "https://count.squachwatch.com/__tally";

function cors(origin) {
  return {
    "Access-Control-Allow-Origin": ORIGINS.has(origin) ? origin : "https://squachwatch.com",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Vary": "Origin",
  };
}

function json(body, origin, extra) {
  return new Response(JSON.stringify(body), {
    headers: { "Content-Type": "application/json", ...cors(origin), ...(extra || {}) },
  });
}

// The table, and the one-time copy from KV. Checked once per worker instance.
let ready = false;
async function ensureReady(env) {
  if (ready) return;
  await env.DB.prepare(
    "CREATE TABLE IF NOT EXISTS counts (board TEXT PRIMARY KEY, n INTEGER NOT NULL DEFAULT 0)"
  ).run();
  const row = await env.DB.prepare("SELECT COUNT(*) AS c FROM counts").first();
  if (row && row.c === 0 && env.COUNTS) {
    const total = parseInt((await env.COUNTS.get("total")) || "0", 10);
    const raw = await env.COUNTS.get("boards");
    const boards = raw ? JSON.parse(raw) : {};
    let sum = 0;
    const seed = [];
    for (const [board, n] of Object.entries(boards)) {
      if (!BOARDS.has(board) || !Number.isFinite(n)) continue;
      seed.push(env.DB.prepare("INSERT OR IGNORE INTO counts (board, n) VALUES (?, ?)").bind(board, n));
      sum += n;
    }
    seed.push(env.DB.prepare("INSERT OR IGNORE INTO counts (board, n) VALUES ('_offset', ?)").bind(total - sum));
    // INSERT OR IGNORE: two instances seeding at once write the same rows once.
    await env.DB.batch(seed);
  }
  ready = true;
}

async function tally(env) {
  const { results } = await env.DB.prepare("SELECT board, n FROM counts").all();
  let total = BASE;
  const boards = {};
  for (const r of results || []) {
    total += r.n;
    if (!r.board.startsWith("_")) boards[r.board] = r.n;
  }
  return { total, boards };
}

export default {
  async fetch(request, env, ctx) {
    const origin = request.headers.get("Origin") || "";
    const url = new URL(request.url);

    if (request.method === "OPTIONS") return new Response(null, { headers: cors(origin) });

    if (request.method === "GET" && url.pathname === "/") {
      const cache = caches.default;
      let hit = await cache.match(CACHE_KEY);
      let body;
      if (hit) {
        body = await hit.json();
      } else {
        await ensureReady(env);
        body = await tally(env);
        const copy = new Response(JSON.stringify(body), {
          headers: { "Content-Type": "application/json", "Cache-Control": "public, max-age=" + CACHE_SECONDS },
        });
        ctx.waitUntil(cache.put(CACHE_KEY, copy));
      }
      return json(body, origin, { "Cache-Control": "public, max-age=" + CACHE_SECONDS });
    }

    if (request.method === "POST" && url.pathname === "/hit") {
      // Only the flasher page counts; a browser always sends its Origin on a POST.
      if (!ORIGINS.has(origin)) return new Response("no", { status: 403 });
      let board = "";
      try { board = String((await request.json()).board || ""); } catch (e) {}
      if (!BOARDS.has(board)) return new Response("unknown board", { status: 400, headers: cors(origin) });
      await ensureReady(env);
      await env.DB.prepare(
        "INSERT INTO counts (board, n) VALUES (?, 1) ON CONFLICT(board) DO UPDATE SET n = n + 1"
      ).bind(board).run();
      const body = await tally(env);
      // The installer sees its own count straight away; everyone else within
      // a minute, when the cached copy runs out.
      ctx.waitUntil(caches.default.delete(CACHE_KEY));
      return json(body, origin);
    }

    return new Response("SquachWatches in the Wild", { status: 404, headers: cors(origin) });
  },
};
