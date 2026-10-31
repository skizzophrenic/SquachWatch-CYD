#include "draw_band.h"

#if SQW_BANDED_FRAME
namespace DrawBand {
// The whole screen until main() says otherwise, so a screen that has not been
// taught about bands draws exactly as it always did.
int16_t g_y0 = 0, g_y1 = 32767;
bool    g_on = true;
}
#endif
