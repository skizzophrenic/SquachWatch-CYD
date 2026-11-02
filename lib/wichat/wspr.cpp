/* wspr.c — WiChat FEC codec: chunked TX, reassembly RX, repetition coding */
#include "wspr.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

void wspr_init(wspr_ctx_t *ctx, int enabled)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->enabled = enabled;
}

/* ------------------------------------------------------------------ */
/*  TX — split message into chunks                                     */
/* ------------------------------------------------------------------ */

int wspr_encode(wspr_tx_t *tx, const uint8_t *msg, size_t msg_len,
                uint32_t parent_msg_id)
{
    if (!tx || !msg || msg_len == 0)
        return -1;

    /* Calculate how many chunks we need */
    uint8_t total = (uint8_t)((msg_len + WSPR_CHUNK_DATA_SIZE - 1) / WSPR_CHUNK_DATA_SIZE);
    if (total > WSPR_MAX_CHUNKS)
        return -1;

    memset(tx, 0, sizeof(*tx));
    tx->parent_msg_id = parent_msg_id;
    tx->total_chunks  = total;

    size_t offset = 0;
    for (uint8_t i = 0; i < total; i++) {
        size_t remaining = msg_len - offset;
        size_t chunk_len = (remaining < WSPR_CHUNK_DATA_SIZE) ? remaining : WSPR_CHUNK_DATA_SIZE;
        memcpy(tx->chunks[i], msg + offset, chunk_len);
        tx->chunk_lens[i] = chunk_len;
        offset += chunk_len;
    }

    return (int)total;
}

/* ------------------------------------------------------------------ */
/*  TX — build a single chunk frame                                    */
/* ------------------------------------------------------------------ */

int wspr_build_chunk_frame(const wspr_tx_t *tx, int chunk_idx,
                           const uint8_t *local_mac, wichat_frame_t *out)
{
    if (!tx || !local_mac || !out)
        return -1;
    if (chunk_idx < 0 || chunk_idx >= tx->total_chunks)
        return -1;

    memset(out, 0, sizeof(*out));

    /* Header */
    out->hdr.magic  = WICHAT_MAGIC;
    out->hdr.msg_id = frame_gen_msg_id();   /* each chunk gets its own ID */
    out->hdr.ttl    = WICHAT_DEFAULT_TTL;
    out->hdr.type   = WICHAT_TYPE_FEC_CHUNK;
    memcpy(out->hdr.sender, local_mac, 6);

    /* Payload: parent_msg_id(4) + seq(1) + total(1) + data(N) */
    uint8_t *p = out->payload;

    /* parent_msg_id — little-endian */
    p[0] = (uint8_t)(tx->parent_msg_id);
    p[1] = (uint8_t)(tx->parent_msg_id >> 8);
    p[2] = (uint8_t)(tx->parent_msg_id >> 16);
    p[3] = (uint8_t)(tx->parent_msg_id >> 24);
    p += 4;

    /* seq */
    *p++ = (uint8_t)chunk_idx;

    /* total */
    *p++ = tx->total_chunks;

    /* chunk data */
    size_t dlen = tx->chunk_lens[chunk_idx];
    memcpy(p, tx->chunks[chunk_idx], dlen);
    p += dlen;

    out->payload_len = (size_t)(p - out->payload);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  RX — parse chunk payload from a frame                              */
/* ------------------------------------------------------------------ */

/* Internal: parse the 6-byte chunk header + data from a frame payload */
static int parse_chunk(const wichat_frame_t *frame,
                       uint32_t *parent_id, uint8_t *seq, uint8_t *total,
                       const uint8_t **data_out, size_t *data_len)
{
    /* Minimum payload: 4 (parent_msg_id) + 1 (seq) + 1 (total) = 6 */
    if (frame->payload_len < 6)
        return -1;

    const uint8_t *p = frame->payload;

    /* parent_msg_id — little-endian */
    *parent_id = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    p += 4;

    *seq   = *p++;
    *total = *p++;

    /* Sanity checks */
    if (*total == 0 || *total > WSPR_MAX_CHUNKS)
        return -1;
    if (*seq >= *total)
        return -1;

    *data_out = p;
    *data_len = frame->payload_len - 6;

    if (*data_len > WSPR_CHUNK_DATA_SIZE)
        *data_len = WSPR_CHUNK_DATA_SIZE;

    return 0;
}

/* Find an existing reassembly session for this parent_msg_id */
static wspr_rx_t *find_session(wspr_ctx_t *ctx, uint32_t parent_id)
{
    for (int i = 0; i < WSPR_MAX_PENDING; i++) {
        if (ctx->pending[i].active && ctx->pending[i].parent_msg_id == parent_id)
            return &ctx->pending[i];
    }
    return NULL;
}

/* Allocate the oldest free slot, or evict the oldest active session */
static wspr_rx_t *alloc_session(wspr_ctx_t *ctx)
{
    /* First pass: find an inactive slot */
    for (int i = 0; i < WSPR_MAX_PENDING; i++) {
        if (!ctx->pending[i].active)
            return &ctx->pending[i];
    }

    /* Second pass: evict oldest (lowest first_seen_ms) */
    int oldest = 0;
    for (int i = 1; i < WSPR_MAX_PENDING; i++) {
        if (ctx->pending[i].first_seen_ms < ctx->pending[oldest].first_seen_ms)
            oldest = i;
    }
    memset(&ctx->pending[oldest], 0, sizeof(wspr_rx_t));
    return &ctx->pending[oldest];
}

/* Check whether all chunks have been received */
static int is_complete(const wspr_rx_t *rx)
{
    for (uint8_t i = 0; i < rx->total_chunks; i++) {
        if (!rx->received[i])
            return 0;
    }
    return 1;
}

int wspr_feed_chunk(wspr_ctx_t *ctx, const wichat_frame_t *frame)
{
    if (!ctx || !frame)
        return -1;
    if (!ctx->enabled)
        return -1;
    if (frame->hdr.type != WICHAT_TYPE_FEC_CHUNK)
        return -1;

    uint32_t parent_id;
    uint8_t  seq, total;
    const uint8_t *data;
    size_t   data_len;

    if (parse_chunk(frame, &parent_id, &seq, &total, &data, &data_len) != 0)
        return -1;

    /* Find or allocate session */
    wspr_rx_t *rx = find_session(ctx, parent_id);
    if (!rx) {
        rx = alloc_session(ctx);
        memset(rx, 0, sizeof(*rx));
        rx->parent_msg_id = parent_id;
        rx->total_chunks  = total;
        rx->first_seen_ms = 0;  /* caller should set via tick; 0 = unknown */
        rx->active        = 1;
    }

    /* Store chunk (ignore duplicates silently) */
    if (!rx->received[seq]) {
        rx->received[seq] = 1;
        memcpy(rx->data[seq], data, data_len);
        rx->data_lens[seq] = data_len;
    }

    /* Check completeness */
    if (is_complete(rx)) {
        rx->complete = 1;
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  RX — extract reassembled message                                   */
/* ------------------------------------------------------------------ */

static const uint8_t MISSING_PLACEHOLDER[] = { '?', '?', '?' };

int wspr_get_message(wspr_ctx_t *ctx, uint32_t parent_msg_id,
                     uint8_t *out, size_t out_max)
{
    if (!ctx || !out || out_max == 0)
        return -1;

    wspr_rx_t *rx = find_session(ctx, parent_msg_id);
    if (!rx)
        return -1;

    size_t written = 0;
    for (uint8_t i = 0; i < rx->total_chunks; i++) {
        if (rx->received[i]) {
            size_t len = rx->data_lens[i];
            if (written + len > out_max)
                len = out_max - written;
            memcpy(out + written, rx->data[i], len);
            written += len;
        } else {
            /* Missing chunk — insert placeholder */
            size_t plen = sizeof(MISSING_PLACEHOLDER);
            if (written + plen > out_max)
                plen = out_max - written;
            memcpy(out + written, MISSING_PLACEHOLDER, plen);
            written += plen;
        }
        if (written >= out_max)
            break;
    }

    /* Free the reassembly slot */
    memset(rx, 0, sizeof(*rx));

    return (int)written;
}

/* ------------------------------------------------------------------ */
/*  Tick — expire stale reassembly sessions                            */
/* ------------------------------------------------------------------ */

void wspr_tick(wspr_ctx_t *ctx, uint32_t now_ms, uint32_t *expired_id)
{
    if (!ctx || !expired_id)
        return;

    *expired_id = 0;

    /* Find the oldest active, non-complete session that has exceeded timeout */
    uint32_t oldest_seen = 0xFFFFFFFF;
    int      oldest_idx  = -1;

    for (int i = 0; i < WSPR_MAX_PENDING; i++) {
        wspr_rx_t *rx = &ctx->pending[i];
        if (!rx->active || rx->complete)
            continue;

        /* Handle first_seen_ms == 0: set it on first tick */
        if (rx->first_seen_ms == 0) {
            rx->first_seen_ms = now_ms;
            continue;
        }

        uint32_t elapsed = now_ms - rx->first_seen_ms;
        if (elapsed >= WICHAT_FEC_TIMEOUT_MS) {
            if (rx->first_seen_ms < oldest_seen) {
                oldest_seen = rx->first_seen_ms;
                oldest_idx  = i;
            }
        }
    }

    if (oldest_idx >= 0) {
        ctx->pending[oldest_idx].complete = 1;
        *expired_id = ctx->pending[oldest_idx].parent_msg_id;
    }
}
