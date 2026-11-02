/* frame.cpp -- WiChat wire format encode/decode (ESP32 CYD port)
 * Identical wire format to PC client. Uses esp_random() for msg IDs. */
#include "frame.h"
#include <string.h>
#include <esp_random.h>

static const uint8_t BSSID[6] = WICHAT_BSSID;
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

int frame_encode(const wichat_frame_t *frame, const uint8_t *local_mac,
                 uint8_t *out_buf, size_t out_max)
{
    size_t total = sizeof(dot11_hdr_t) + sizeof(wichat_hdr_t) + frame->payload_len;
    if (total > out_max)
        return -1;

    uint8_t *p = out_buf;

    /* Build 802.11 header */
    dot11_hdr_t dot11;
    memset(&dot11, 0, sizeof(dot11));
    dot11.frame_control = 0x0008; /* Data frame */
    dot11.duration = 0;
    memcpy(dot11.da, BROADCAST, 6);
    memcpy(dot11.sa, local_mac, 6);
    memcpy(dot11.bssid, BSSID, 6);
    dot11.seq_ctrl = 0;

    memcpy(p, &dot11, sizeof(dot11));
    p += sizeof(dot11);

    /* WiChat header */
    memcpy(p, &frame->hdr, sizeof(wichat_hdr_t));
    p += sizeof(wichat_hdr_t);

    /* Payload */
    if (frame->payload_len > 0) {
        memcpy(p, frame->payload, frame->payload_len);
        p += frame->payload_len;
    }

    return (int)(p - out_buf);
}

int frame_decode(const uint8_t *raw, size_t raw_len, wichat_frame_t *frame)
{
    /* Need at least dot11 + wichat header */
    if (raw_len < sizeof(dot11_hdr_t) + sizeof(wichat_hdr_t))
        return -1;

    const dot11_hdr_t *dot11 = (const dot11_hdr_t *)raw;

    /* Validate BSSID */
    if (memcmp(dot11->bssid, BSSID, 6) != 0)
        return -1;

    const uint8_t *hdr_ptr = raw + sizeof(dot11_hdr_t);
    memcpy(&frame->hdr, hdr_ptr, sizeof(wichat_hdr_t));

    /* Validate magic */
    if (frame->hdr.magic != WICHAT_MAGIC)
        return -1;

    /* Extract payload */
    size_t payload_offset = sizeof(dot11_hdr_t) + sizeof(wichat_hdr_t);
    size_t payload_len = raw_len - payload_offset;
    if (payload_len > WICHAT_MAX_PAYLOAD)
        payload_len = WICHAT_MAX_PAYLOAD;

    frame->payload_len = payload_len;
    if (payload_len > 0)
        memcpy(frame->payload, raw + payload_offset, payload_len);

    return 0;
}

uint32_t frame_gen_msg_id(void)
{
    // Never return 0: cyd_mesh_is_seen() treats id 0 as "never seen", so a
    // id-0 frame would bypass dedup and be relayed by every node on every
    // reception (a mini broadcast storm bounded only by TTL).
    uint32_t id;
    do { id = esp_random(); } while (id == 0);
    return id;
}
