/* mesh.cpp -- WiChat mesh engine for ESP32 CYD
 * Port of PC mesh.c -- calls radio_esp32_tx() directly, uses millis(). */
#include "mesh.h"
#include "wspr.h"
#include "radio_esp32.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>

/* FEC reassembly context (~30 KB). Only needed while reassembling a
 * multi-frame message, which is rare — most mesh traffic fits in one frame.
 * Allocated lazily on the heap on first FEC chunk, so it costs nothing (and
 * never sits on the static DRAM segment) in the common single-frame case.
 * The TX path uses its own stack-local wspr_tx_t and does not need this. */
static wspr_ctx_t *fec_ctx = nullptr;
#define FEC_REPETITIONS 3

/* Allocate + init the FEC context on first use. Returns false if the heap
 * allocation fails, in which case the caller drops the FEC chunk gracefully. */
static bool fec_ensure(void)
{
    if (!fec_ctx) {
        fec_ctx = (wspr_ctx_t *)calloc(1, sizeof(wspr_ctx_t));
        if (fec_ctx) wspr_init(fec_ctx, 1);
    }
    return fec_ctx != nullptr;
}

/* Global mesh instance */
cyd_mesh_t g_mesh;

/* ---- Static helpers ---- */

static void add_message(const char *sender, const char *text, uint32_t mid, uint8_t status)
{
    cyd_mesh_message_t *msg = &g_mesh.messages[g_mesh.msg_head];
    memset(msg, 0, sizeof(*msg));

    strncpy(msg->sender, sender, WICHAT_MAX_NAME_LEN - 1);
    msg->sender[WICHAT_MAX_NAME_LEN - 1] = '\0';

    strncpy(msg->text, text, WICHAT_MAX_PAYLOAD);
    msg->text[WICHAT_MAX_PAYLOAD] = '\0';

    msg->timestamp_ms = millis();
    msg->msg_id = mid;
    msg->status = status;

    g_mesh.msg_head = (g_mesh.msg_head + 1) % WICHAT_MAX_MESSAGES;
    if (g_mesh.msg_count < WICHAT_MAX_MESSAGES)
        g_mesh.msg_count++;

    g_mesh.msg_version++;
}

static cyd_mesh_node_t *find_or_add_node(const uint8_t *mac)
{
    int empty_slot = -1;

    for (int i = 0; i < MESH_MAX_NODES; i++) {
        if (g_mesh.nodes[i].active &&
            memcmp(g_mesh.nodes[i].mac, mac, 6) == 0)
            return &g_mesh.nodes[i];
        if (!g_mesh.nodes[i].active && empty_slot < 0)
            empty_slot = i;
    }

    if (empty_slot < 0)
        return NULL;

    cyd_mesh_node_t *n = &g_mesh.nodes[empty_slot];
    memset(n, 0, sizeof(*n));
    memcpy(n->mac, mac, 6);
    n->active = 1;
    g_mesh.node_count++;
    return n;
}

static const char *resolve_sender(const uint8_t *mac)
{
    static char hex_buf[16];

    for (int i = 0; i < MESH_MAX_NODES; i++) {
        if (g_mesh.nodes[i].active &&
            memcmp(g_mesh.nodes[i].mac, mac, 6) == 0 &&
            g_mesh.nodes[i].callsign[0] != '\0')
            return g_mesh.nodes[i].callsign;
    }

    snprintf(hex_buf, sizeof(hex_buf), "%02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    return hex_buf;
}

int mesh_transmit(const wichat_frame_t *frame)
{
    uint8_t buf[512];
    int n = frame_encode(frame, g_mesh.local_mac, buf, sizeof(buf));
    if (n < 0)
        return -1;
    return radio_esp32_tx(buf, (size_t)n);
}

/* ---- Public API ---- */

void cyd_mesh_init(const char *callsign)
{
    memset(&g_mesh, 0, sizeof(g_mesh));
    g_mesh.channel = WICHAT_DEFAULT_CH;
    g_mesh.ttl     = WICHAT_DEFAULT_TTL;
    /* FEC context is allocated lazily (fec_ensure) on first chunk received. */

    if (callsign) {
        strncpy(g_mesh.callsign, callsign, WICHAT_MAX_NAME_LEN - 1);
        g_mesh.callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
    } else {
        strncpy(g_mesh.callsign, WICHAT_DEFAULT_NAME, WICHAT_MAX_NAME_LEN - 1);
        g_mesh.callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
    }

    /* Get real MAC from ESP32 WiFi */
    radio_esp32_get_mac(g_mesh.local_mac);
}

int cyd_mesh_is_seen(uint32_t msg_id)
{
    for (int i = 0; i < WICHAT_DEDUP_SIZE; i++) {
        if (g_mesh.seen_ids[i] == msg_id && msg_id != 0)
            return 1;
    }
    return 0;
}

void cyd_mesh_mark_seen(uint32_t msg_id)
{
    g_mesh.seen_ids[g_mesh.seen_head] = msg_id;
    g_mesh.seen_head = (g_mesh.seen_head + 1) % WICHAT_DEDUP_SIZE;
}

int cyd_mesh_process_rx(const uint8_t *raw, size_t len, int8_t rssi)
{
    wichat_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    if (frame_decode(raw, len, &frame) != 0)
        return -1;

    /* Skip our own frames */
    if (memcmp(frame.hdr.sender, g_mesh.local_mac, 6) == 0)
        return -1;

    /* Dedup */
    if (cyd_mesh_is_seen(frame.hdr.msg_id))
        return -1;

    cyd_mesh_mark_seen(frame.hdr.msg_id);

    /* Handle by type */
    switch (frame.hdr.type) {

    case WICHAT_TYPE_BEACON: {
        cyd_mesh_node_t *node = find_or_add_node(frame.hdr.sender);
        if (node) {
            node->last_seen_ms = millis();
            node->rssi = rssi;
            if (frame.payload_len >= WICHAT_MAX_NAME_LEN) {
                memcpy(node->callsign, frame.payload, WICHAT_MAX_NAME_LEN - 1);
                node->callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
            }
        }
        break;
    }

    case WICHAT_TYPE_CHAT: {
        const char *sender_name = resolve_sender(frame.hdr.sender);
        char text_buf[WICHAT_MAX_PAYLOAD + 1];
        memset(text_buf, 0, sizeof(text_buf));
        if (frame.payload_len > 0) {
            size_t copy_len = frame.payload_len;
            if (copy_len > WICHAT_MAX_PAYLOAD)
                copy_len = WICHAT_MAX_PAYLOAD;
            memcpy(text_buf, frame.payload, copy_len);
        }
        add_message(sender_name, text_buf, frame.hdr.msg_id, MSG_STATUS_NONE);

        /* Send ACK back to sender */
        {
            wichat_frame_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.hdr.magic  = WICHAT_MAGIC;
            ack.hdr.msg_id = frame_gen_msg_id();
            ack.hdr.ttl    = g_mesh.ttl;
            ack.hdr.type   = WICHAT_TYPE_ACK;
            memcpy(ack.hdr.sender, g_mesh.local_mac, 6);
            /* ACK payload: original msg_id (4 bytes) + original sender MAC (6 bytes) */
            ack.payload[0] = (uint8_t)(frame.hdr.msg_id);
            ack.payload[1] = (uint8_t)(frame.hdr.msg_id >> 8);
            ack.payload[2] = (uint8_t)(frame.hdr.msg_id >> 16);
            ack.payload[3] = (uint8_t)(frame.hdr.msg_id >> 24);
            memcpy(ack.payload + 4, frame.hdr.sender, 6);
            ack.payload_len = 10;
            cyd_mesh_mark_seen(ack.hdr.msg_id);
            mesh_transmit(&ack);
            Serial.printf("[ACK] Sent ACK for msg %08x\n", frame.hdr.msg_id);
        }
        break;
    }

    case WICHAT_TYPE_PING: {
        wichat_frame_t pong;
        memset(&pong, 0, sizeof(pong));
        pong.hdr.magic  = WICHAT_MAGIC;
        pong.hdr.msg_id = frame_gen_msg_id();
        pong.hdr.ttl    = g_mesh.ttl;
        pong.hdr.type   = WICHAT_TYPE_PONG;
        memcpy(pong.hdr.sender, g_mesh.local_mac, 6);
        memcpy(pong.payload, frame.hdr.sender, 6);
        pong.payload_len = 6;

        cyd_mesh_mark_seen(pong.hdr.msg_id);
        mesh_transmit(&pong);

        const char *from = resolve_sender(frame.hdr.sender);
        char sys_buf[128];
        snprintf(sys_buf, sizeof(sys_buf), "PING from %s (RSSI %d) - PONG sent",
                 from, (int)rssi);
        add_message("[system]", sys_buf, 0, MSG_STATUS_NONE);
        break;
    }

    case WICHAT_TYPE_PONG: {
        if (frame.payload_len >= 6 &&
            memcmp(frame.payload, g_mesh.local_mac, 6) == 0) {
            const char *from = resolve_sender(frame.hdr.sender);
            char sys_buf[128];
            snprintf(sys_buf, sizeof(sys_buf), "PONG from %s (RSSI %d)",
                     from, (int)rssi);
            add_message("[system]", sys_buf, 0, MSG_STATUS_NONE);
        }
        cyd_mesh_node_t *node = find_or_add_node(frame.hdr.sender);
        if (node) {
            node->last_seen_ms = millis();
            node->rssi = rssi;
        }
        break;
    }

    case WICHAT_TYPE_ACK: {
        /* ACK payload: original msg_id (4 bytes) + original sender MAC (6 bytes) */
        if (frame.payload_len >= 10) {
            uint32_t acked_id = (uint32_t)frame.payload[0]
                              | ((uint32_t)frame.payload[1] << 8)
                              | ((uint32_t)frame.payload[2] << 16)
                              | ((uint32_t)frame.payload[3] << 24);
            /* Check if ACK is for us (compare sender MAC in ACK payload to our MAC) */
            if (memcmp(frame.payload + 4, g_mesh.local_mac, 6) == 0) {
                /* Find the message and mark as ACKED */
                for (int i = 0; i < g_mesh.msg_count && i < WICHAT_MAX_MESSAGES; i++) {
                    int idx = (g_mesh.msg_head - 1 - i + WICHAT_MAX_MESSAGES) % WICHAT_MAX_MESSAGES;
                    if (g_mesh.messages[idx].msg_id == acked_id &&
                        g_mesh.messages[idx].status == MSG_STATUS_PENDING) {
                        g_mesh.messages[idx].status = MSG_STATUS_ACKED;
                        g_mesh.msg_version++;  /* trigger display refresh */
                        Serial.printf("[ACK] Message %08x delivered!\n", acked_id);
                        break;
                    }
                }
            }
        }
        break;
    }

    case WICHAT_TYPE_FEC_CHUNK: {
        if (!fec_ensure()) break;   // low heap: drop the chunk rather than crash
        int fec_rc = wspr_feed_chunk(fec_ctx, &frame);
        if (fec_rc == 1) {
            uint32_t parent_id = 0;
            if (frame.payload_len >= 4) {
                parent_id = (uint32_t)frame.payload[0]
                          | ((uint32_t)frame.payload[1] << 8)
                          | ((uint32_t)frame.payload[2] << 16)
                          | ((uint32_t)frame.payload[3] << 24);
            }
            if (!cyd_mesh_is_seen(parent_id)) {
                cyd_mesh_mark_seen(parent_id);
                uint8_t msg_buf[WICHAT_MAX_PAYLOAD + 1];
                memset(msg_buf, 0, sizeof(msg_buf));
                int msg_len = wspr_get_message(fec_ctx, parent_id,
                                                msg_buf, WICHAT_MAX_PAYLOAD);
                if (msg_len > 0) {
                    const char *sender_name = resolve_sender(frame.hdr.sender);
                    add_message(sender_name, (const char *)msg_buf, parent_id, MSG_STATUS_NONE);
                }
            }
        }
        break;
    }

    default:
        break;
    }

    /* ---- Flood relay ---- */
    if (frame.hdr.ttl > 1) {
        wichat_frame_t relay;
        memcpy(&relay, &frame, sizeof(relay));
        relay.hdr.ttl = frame.hdr.ttl - 1;
        mesh_transmit(&relay);
    }

    return 0;
}

int cyd_mesh_send_chat(const char *text)
{
    if (!text || text[0] == '\0')
        return -1;

    wichat_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = g_mesh.ttl;
    frame.hdr.type   = WICHAT_TYPE_CHAT;
    memcpy(frame.hdr.sender, g_mesh.local_mac, 6);

    size_t text_len = strlen(text) + 1;  /* include null terminator */
    if (text_len > WICHAT_MAX_PAYLOAD)
        text_len = WICHAT_MAX_PAYLOAD;

    memcpy(frame.payload, text, text_len);
    frame.payload_len = text_len;

    add_message(g_mesh.callsign, text, frame.hdr.msg_id, MSG_STATUS_PENDING);
    cyd_mesh_mark_seen(frame.hdr.msg_id);
    mesh_transmit(&frame);

    /* FEC: send 3x redundant chunks */
    {
        wspr_tx_t fec_tx;
        int nchunks = wspr_encode(&fec_tx, (const uint8_t *)text, text_len,
                                   frame.hdr.msg_id);
        if (nchunks > 0) {
            for (int rep = 0; rep < FEC_REPETITIONS; rep++) {
                for (int ci = 0; ci < nchunks; ci++) {
                    wichat_frame_t chunk_frame;
                    if (wspr_build_chunk_frame(&fec_tx, ci, g_mesh.local_mac,
                                               &chunk_frame) == 0) {
                        cyd_mesh_mark_seen(chunk_frame.hdr.msg_id);
                        mesh_transmit(&chunk_frame);
                    }
                }
            }
        }
    }

    return 0;
}

int cyd_mesh_send_beacon(void)
{
    wichat_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = g_mesh.ttl;
    frame.hdr.type   = WICHAT_TYPE_BEACON;
    memcpy(frame.hdr.sender, g_mesh.local_mac, 6);

    wichat_beacon_t beacon;
    memset(&beacon, 0, sizeof(beacon));
    strncpy(beacon.callsign, g_mesh.callsign, sizeof(beacon.callsign) - 1);
    beacon.flags = 0;
    beacon.uptime_sec = millis() / 1000;

    memcpy(frame.payload, &beacon, sizeof(beacon));
    frame.payload_len = sizeof(beacon);

    cyd_mesh_mark_seen(frame.hdr.msg_id);
    mesh_transmit(&frame);

    return 0;
}

int cyd_mesh_send_ping(void)
{
    wichat_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = g_mesh.ttl;
    frame.hdr.type   = WICHAT_TYPE_PING;
    memcpy(frame.hdr.sender, g_mesh.local_mac, 6);
    frame.payload_len = 0;

    cyd_mesh_mark_seen(frame.hdr.msg_id);
    add_message("[system]", "PING sent...", 0, MSG_STATUS_NONE);
    mesh_transmit(&frame);

    return 0;
}

void cyd_mesh_tick(uint32_t now_ms)
{
    /* Send beacon if interval elapsed */
    if (now_ms - g_mesh.last_beacon_ms >= WICHAT_BEACON_INTERVAL_MS) {
        cyd_mesh_send_beacon();
        g_mesh.last_beacon_ms = now_ms;
    }

    /* Mark pending messages as FAILED after 5 seconds with no ACK */
    for (int i = 0; i < g_mesh.msg_count && i < WICHAT_MAX_MESSAGES; i++) {
        int idx = (g_mesh.msg_head - 1 - i + WICHAT_MAX_MESSAGES) % WICHAT_MAX_MESSAGES;
        if (g_mesh.messages[idx].status == MSG_STATUS_PENDING &&
            now_ms - g_mesh.messages[idx].timestamp_ms > 5000) {
            g_mesh.messages[idx].status = MSG_STATUS_FAILED;
            g_mesh.msg_version++;
        }
    }

    /* Expire stale nodes every 10 seconds */
    static uint32_t last_expire_ms = 0;
    if (now_ms - last_expire_ms >= 10000) {
        last_expire_ms = now_ms;
        for (int i = 0; i < MESH_MAX_NODES; i++) {
            if (g_mesh.nodes[i].active) {
                uint32_t age_ms = now_ms - g_mesh.nodes[i].last_seen_ms;
                if (age_ms > WICHAT_NODE_EXPIRE_MS) {
                    memset(&g_mesh.nodes[i], 0, sizeof(cyd_mesh_node_t));
                    if (g_mesh.node_count > 0)
                        g_mesh.node_count--;
                }
            }
        }
    }
}
