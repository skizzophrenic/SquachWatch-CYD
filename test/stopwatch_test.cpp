#include <stdio.h>
#include "stopwatch_geometry.h"
#include "round_layout.h"
#include "touch_fit.h"
#include "qwerty.h"
static int failures = 0;
static void ck(const char* message, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}
int main() {
    using namespace StopWatchGeometry;
    int16_t x = -1, y = -1;
    ck("native center is not shifted", toLogical(233,233,x,y) && x==233 && y==233);
    ck("driver padding removed exactly once", toLogical(234,234,x,y,468,468) && x==233 && y==233);
    ck("square corners reject touch", !toLogical(0,0,x,y) && !toLogical(465,465,x,y));
    ck("glass edges are reachable", toLogical(0,233,x,y) && x==0 && toLogical(465,233,x,y) && x==465);
    ck("invalid controller coordinates reject touch", !toLogical(-1,233,x,y) && !toLogical(466,233,x,y));
    bool native=true, fitOk=true;
    auto fit=TouchFit::fromRanges(0,WIDTH,0,HEIGHT,WIDTH,HEIGHT);
    for(int py=0;py<PANEL;++py) for(int px=0;px<PANEL;++px) {
        const bool accepted=toLogical(px+1,py+1,x,y,468,468);
        native &= accepted==visible(px,py);
        if(accepted) {
            native &= x==px && y==py;
            float sx,sy; TouchFit::toScreen(fit,x,y,0,sx,sy);
            fitOk &= sx==px && sy==py;
        }
    }
    ck("every glass pixel has identity touch mapping",native);
    ck("app calibration preserves native coordinates",fitOk);
    bool chords=true;
    for(int top=72;top<370;top+=3) for(int h=14;h<65;h+=5) {
        if(top+h>394) continue;
        int margin=RoundLayout::inset(top,h);
        chords &= margin>0 && margin<180;
        chords &= visible(margin,top) && visible(PANEL-margin-1,top+h-1);
    }
    ck("whole menu row rectangles fit the glass",chords);
    for(int message=0;message<2;++message) {
        Qwerty::Key keys[Qwerty::KEY_N];
        const int n=Qwerty::layout(PANEL,52,428,keys,message);
        bool inside=true,hits=true,noOverlap=true;
        for(int i=0;i<n;++i) {
            auto k=keys[i];
            inside &= k.w>=10 && k.h>=26 && visible(k.x,k.y) && visible(k.x+k.w-1,k.y+k.h-1);
            hits &= Qwerty::keyAt(keys,n,k.x+k.w/2,k.y+k.h/2,0)==i;
            if(i && k.y==keys[i-1].y) noOverlap &= k.x>=keys[i-1].x+keys[i-1].w;
        }
        ck(message ? "message keys fit circle" : "name keys fit circle",inside);
        ck("every key center selects its own key",hits);
        ck("keyboard keys do not overlap",noOverlap);
    }
    return failures ? 1 : 0;
}
