#pragma once
#include <math.h>
namespace StopWatchArc {
constexpr float RADIANS_PI = 3.14159265358979323846f;
struct Point { float x, y; };
struct Arc { float radius, start, end, half; };
inline Point point(float radius, float angle) {
    const float a = angle * RADIANS_PI / 180;
    return {232.5f + radius * sinf(a), 232.5f + radius * cosf(a)};
}
inline bool contains(const Arc& a, float x, float y) {
    const float dx = x - 232.5f, dy = y - 232.5f;
    float angle = atan2f(dx, dy) * 180 / RADIANS_PI;
    const float mid = (a.start + a.end) * .5f;
    while (angle < mid - 180) angle += 360;
    while (angle > mid + 180) angle -= 360;
    if (angle < a.start) angle = a.start;
    if (angle > a.end) angle = a.end;
    const Point p = point(a.radius, angle);
    return (x-p.x)*(x-p.x) + (y-p.y)*(y-p.y) <= a.half*a.half;
}
inline Arc footer(int index, int count) {
    // Keep the outer edge 25 pixels inside the glass. The smaller angular
    // spread also keeps side buttons below the content area as they move in.
    const float span = count == 1 ? 76.0f : 100.0f;
    const float slot = span / count;
    const float center = -span*.5f + slot * (index + .5f);
    const float inset = 11.2f;
    return {179, center - slot*.5f + inset, center + slot*.5f - inset, 29};
}
inline Arc menu() { return {181, 166, 194, 22}; }
}
