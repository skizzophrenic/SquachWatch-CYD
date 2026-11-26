# SquachWatches in the Wild: the install counter

`worker.js` is the Cloudflare Worker behind **count.squachwatch.com**. The web
flasher sends one `POST /hit` naming the board when an install finishes, and
reads `GET /` for the total and the busiest boards. It stores counts per board
and nothing else: no address, no browser, no time.

Since 2026-10-20 the counts live in a **D1 database** (bound as `DB`). They
used to live in KV (bound as `COUNTS`), whose free plan allows 1,000 writes a
day; every install cost two, so it warned at half its quota after a day and a
half. D1's free plan allows 100,000 rows written a day, and an install is one.

## Moving from KV to D1 (once)

The new worker copies the KV numbers across by itself the first time it runs,
as long as the old KV namespace is still connected. Nothing to type by hand.

1. **Create the database.** Cloudflare dashboard → *Storage & databases* →
   *D1 SQL database* → *Create*. Name it `squachwatch-counts` (any name works).
   Leave the location on automatic.
2. **Connect it to the worker.** *Workers & Pages* → `squachwatch-count` →
   *Settings* → *Bindings* → *Add* → *D1 database*. Variable name **`DB`**
   (capitals, exactly), database: the one from step 1. Save.
   **Leave the existing KV binding `COUNTS` where it is** for now: it is what
   the copy reads from.
3. **Put in the new code.** `squachwatch-count` → *Edit code*. Select all,
   paste the whole of `worker.js` from this folder, then *Deploy*.
4. **Check it.** Open https://count.squachwatch.com/ in a browser. The total
   should match what the flasher showed before (5,468 or more on 2026-10-20),
   with the same boards. That first visit is what did the copy.
5. **A day or so later**, once the numbers on squachwatch.com are still right,
   go back to *Settings* → *Bindings* and remove `COUNTS`. Keep the KV
   namespace itself for a week as a backup, then delete it if you like.

If step 4 shows 5,000 and no boards, the `COUNTS` binding was missing when the
copy ran. Put it back, empty the table (D1 → the database → *Console*:
`DELETE FROM counts;`), press *Deploy* again so the worker starts fresh, then
open the address again.

## Adding a board

A new board on the flasher must also go in `BOARDS` at the top of `worker.js`,
pasted and deployed as in step 3, or its installs are refused and not counted.
The flasher page names it in `NAMES` in `web-flasher/index.html`.

## How it works

- One table, `counts (board, n)`. An install adds one to its board's row in a
  single statement, so two installs at the same moment can't lose a count.
- The total is 5,000 (a guess at installs before counting began) plus every
  row. A hidden `_offset` row holds the difference KV had between its total
  and the sum of its boards (one, on 2026-10-20).
- The tally is kept at the edge for a minute, so page visits barely touch the
  database. An install clears it, so the person installing sees their count.
- Only https://squachwatch.com may count, and only boards in `BOARDS`.

Free-plan limits it works within: 100,000 Worker requests, 100,000 D1 rows
written and 5 million rows read a day, all resetting at midnight UTC.
