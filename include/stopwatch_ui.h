#pragma once
#if defined(M5_STOPWATCH)
#include <TFT_eSPI.h>
#include "stopwatch_arc.h"
namespace StopWatchUI {
struct Rect { int16_t x, y, w, h; };
void beginFrame(uint16_t context);
void endFrame(TFT_eSPI& t);
bool endFrameIfCurrent(TFT_eSPI& t, uint16_t activeContext);
bool focusReady(uint16_t activeContext, uint16_t navigationContext);
void modal(); // Discard controls covered by a modal.
uint16_t context();
// Records actual drawn controls; rim buttons are drawn together at frame end.
bool button(int x, int y, int w, int h, const char* label, bool pressed);
uint8_t controls(Rect* out, uint8_t cap);
bool mapTouch(int& x, int& y);
bool touchPoint(uint8_t index, int& x, int& y);
bool touchSample(bool down, int& x, int& y);
bool focus(TFT_eSPI& t, int logicalX, int logicalY);
void holdProgress(TFT_eSPI& t, uint16_t left, uint16_t right);
void drawArc(TFT_eSPI& t, const StopWatchArc::Arc& arc, uint16_t fill, uint16_t border);
void arcText(TFT_eSPI& t, const StopWatchArc::Arc& arc, const char* label, uint16_t color, bool top = false, bool shadow = false);
void menu(TFT_eSPI& t);
StopWatchArc::Arc menuArc();
void arcControl(const StopWatchArc::Arc& arc, const char* label);
void outlineArc(TFT_eSPI& t, const StopWatchArc::Arc& arc, uint16_t color);
}
#endif
