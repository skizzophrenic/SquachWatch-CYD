#pragma once

#if defined(CYD35) || defined(CYD_GPS_ELECROW)
#define SQW_BANDED_FRAME 1
#else
#define SQW_BANDED_FRAME 0
#endif

namespace FrameLayout {

// Allocate the same number of pixels whichever orientation boots first.
// A portrait-sized allocation rounded down to complete rows would be 80
// bytes too small when subsequently reshaped to landscape.
constexpr int allocationWidth(int screenW) {
#if defined(CYD_GPS_ELECROW)
    return 320;
#else
    return screenW;
#endif
}
constexpr int allocationHeight(int screenH) {
#if defined(CYD_GPS_ELECROW)
    return 160;
#elif defined(CYD35)
    return screenH / 2;
#else
    return screenH;
#endif
}
constexpr int bufferHeight(int screenW, int screenH) {
#if defined(CYD_GPS_ELECROW)
    return screenW > 0 ? 51200 / screenW : 0;
#elif defined(CYD35)
    return screenH / 2;
#else
    return screenH;
#endif
}

} // namespace FrameLayout
