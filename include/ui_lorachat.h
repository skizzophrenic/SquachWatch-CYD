// SquachWatch-CYD — LORA CHATS: what the LoRa listener decoded, one chat per
// network. Meshtastic's open channels in one tab, MeshCore's Public and
// hashtag channels in the other, newest at the top. Read only: the radio
// never transmits (include/lora_sniffer.h). Built only with SQUACH_LORA.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "lora_pkt.h"

enum class LoraChatHit : uint8_t { NONE = 0, TAB_MESHTASTIC, TAB_MESHCORE, CHANNEL, TAB_DMS };

void        uiLoraChatInit(TFT_eSPI& t);
void        uiLoraChatTick(TFT_eSPI& t, uint32_t now);
void        uiLoraChatScroll(int delta);   // positive = older
LoraChatHit uiLoraChatHit(TFT_eSPI& t, int x, int y);
void        uiLoraChatSelect(uint8_t tab); // 0 Meshtastic, 1 MeshCore, 2 DMs (the list)
// The DMs tab, open on the conversation with that Meshtastic node.
void        uiLoraChatOpenDm(uint32_t peer, Lora::Proto p = Lora::Proto::MESHTASTIC);
// Private messages to us not yet looked at, every conversation together.
uint16_t    uiLoraChatDmUnread();
// A tap that is not a tab or the chip: a conversation in the DMs list (opens
// it), "< ALL DMs" (back to the list), a line in the MESHTASTIC tab (opens a
// conversation with whoever said it). True when it did something.
bool        uiLoraChatTap(int x, int y, uint32_t now);
// When that conversation was last open (millis), for the LORA page's NEW.
uint32_t    uiLoraChatDmSeenMs(uint32_t peer);

// Messages decoded since the chats were last open, both networks together,
// for the LORA CHATS row.
uint16_t    uiLoraChatUnread();
// The T-Deck's keyboard while the chats are up: printable keys type, '\b'
// deletes, '\n' sends what is typed on the open tab's network.
void        uiLoraChatKey(char c, uint32_t now);
// The T-Deck compose line's channel chip (CHANNEL above): step the open tab's
// network to its next channel that can be sent on (Lora::Chan::txStep).
void        uiLoraChatStepChannel(uint32_t now);
