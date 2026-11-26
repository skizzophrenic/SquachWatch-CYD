// SquachWatch-CYD -- SHARE LOGS, from Settings -> SYSTEM (-DSQW_LOG_SHARE=1 builds).
//
// What to type on the computer: the address, the user and the code, and how
// long sharing has left. One button: STOP, or OK once it has failed. The
// sharing itself is LogShare (log_share.h); this only shows it.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class LogShareHit : uint8_t { NONE, STOP };

void        uiLogShareInit(TFT_eSPI& t);
void        uiLogShareTick(TFT_eSPI& t, uint32_t now);
LogShareHit uiLogShareHit(TFT_eSPI& t, int x, int y);
