#if defined(M5_STOPWATCH)
#include "stopwatch_ui.h"
#include "theme.h"
#include "settings.h"
#include <string.h>

namespace StopWatchUI {
namespace {
struct Control { Rect box; char label[32]; bool rim, pressed, custom; StopWatchArc::Arc arc; };
constexpr uint8_t MAX = 64;
Control pending[MAX], visible[MAX];
uint8_t count = 0, visibleCount = 0;
uint16_t pendingContext = 0xffff, visibleContext = 0xffff;
bool in(const Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h; }
// Prefer an exact arc hit. Outside it, allow eight pixels of finger error,
// but reject ambiguous padding between neighbours instead of guessing.
int rimAt(int x,int y) {
    for(uint8_t i=0;i<visibleCount;++i)
        if(visible[i].rim && StopWatchArc::contains(visible[i].arc,x,y)) return i;
    int hit=-1;
    for(uint8_t i=0;i<visibleCount;++i) if(visible[i].rim) {
        auto padded=visible[i].arc; padded.half+=8;
        if(StopWatchArc::contains(padded,x,y)) { if(hit>=0) return -1; hit=i; }
    }
    return hit;
}
void stroke(TFT_eSPI& t, const StopWatchArc::Arc& a, int radius, uint16_t color) {
    const int steps = (int)ceilf((a.end-a.start)/2);
    for (int i=0;i<=steps;++i) {
        const auto p = StopWatchArc::point(a.radius, a.start + (a.end-a.start)*i/(steps ? steps : 1));
        t.fillCircle((int)lroundf(p.x), (int)lroundf(p.y), radius, color);
    }
}
}
void beginFrame(uint16_t ctx) { count = 0; pendingContext = ctx; }
void modal() { count=0; }
uint16_t context() { return visibleContext; }
bool focusReady(uint16_t activeContext,uint16_t navigationContext) {
    return visibleCount && visibleContext==activeContext && navigationContext==activeContext;
}
bool endFrameIfCurrent(TFT_eSPI& t,uint16_t activeContext) {
    if(pendingContext!=activeContext) {
        // A screen's input handler opened another screen after drawing.
        // Its init may have cleared the sprite. Keep the last complete
        // hardware frame and accept no input until the new screen is drawn.
        count=visibleCount=0;visibleContext=0xffff;
        return false;
    }
    endFrame(t);
    return true;
}
bool button(int x, int y, int w, int h, const char* label, bool pressed) {
    if (w <= 0 || h <= 0 || !label) return false;
    const bool rim = y + h/2 >= 375 && h >= 20 && w >= 40;
    uint8_t slot = count;
    for (uint8_t i=0;i<count;++i) {
        const auto& r = pending[i].box;
        if (r.x==x && r.y==y && r.w==w && r.h==h) { slot=i;break; }
    }
    if (slot >= MAX) return false;
    if (slot == count) ++count;
    auto& c = pending[slot]; c.box={(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)h};
    c.rim=rim; c.pressed=pressed; c.custom=false;
    // Brackets are redundant on a large, outlined touch control.
    size_t j=0;
    for (const char* p=label;*p && j<sizeof(c.label)-1;++p)
        if (*p!='[' && *p!=']' && (j || *p!=' ')) c.label[j++]=*p;
    while (j && c.label[j-1]==' ') --j;
    c.label[j]=0;
    if(rim && strcmp(c.label,"REMOVE FROM WATCH LIST")==0) strcpy(c.label,"UNWATCH");
    if(rim && strcmp(c.label,"REMOVE")==0) strcpy(c.label,"DEL");
    return rim;
}
StopWatchArc::Arc menuArc() {
    return pendingContext / 64 == (uint16_t)AppState::CLEAR && !Settings::boringMode()
        ? StopWatchArc::Arc{112,166,194,24} : StopWatchArc::menu();
}
void arcControl(const StopWatchArc::Arc& arc,const char* label) {
    if(count>=MAX) return;
    auto& c=pending[count++]; const auto p=StopWatchArc::point(arc.radius,(arc.start+arc.end)*.5f);
    c.box={(int16_t)(p.x-12),(int16_t)(p.y-12),24,24};
    c.rim=false;c.custom=true;c.pressed=false;c.arc=arc;
    strncpy(c.label,label,sizeof(c.label)-1);c.label[sizeof(c.label)-1]=0;
}
void outlineArc(TFT_eSPI& t,const StopWatchArc::Arc& a,uint16_t color) {
    auto line=[&](StopWatchArc::Point p,StopWatchArc::Point q) {
        t.drawLine(lroundf(p.x),lroundf(p.y),lroundf(q.x),lroundf(q.y),color);
    };
    for(int edge=-1;edge<=1;edge+=2) for(float angle=a.start;angle<a.end;angle+=1) {
        const float end=angle+1<a.end ? angle+1:a.end;
        line(StopWatchArc::point(a.radius+edge*a.half,angle),StopWatchArc::point(a.radius+edge*a.half,end));
    }
    for(int side=0;side<2;++side) {
        const float angle=side?a.end:a.start;
        const auto p=StopWatchArc::point(a.radius,angle);
        for(int i=0;i<180;++i) {
            const float aa=(angle+(side?0:180)+i)*StopWatchArc::RADIANS_PI/180;
            const float bb=aa+StopWatchArc::RADIANS_PI/180;
            line({p.x+a.half*sinf(aa),p.y+a.half*cosf(aa)}, {p.x+a.half*sinf(bb),p.y+a.half*cosf(bb)});
        }
    }
}
void drawArc(TFT_eSPI& t, const StopWatchArc::Arc& a, uint16_t fill, uint16_t border) {
    stroke(t,a,(int)a.half,border);
    stroke(t,a,(int)a.half-2,fill);
}
void arcText(TFT_eSPI& t, const StopWatchArc::Arc& a, const char* label, uint16_t color, bool top, bool shadow) {
    static TFT_eSprite glyph(&t);
    static bool ready=false;
    if (!ready) { glyph.setColorDepth(16); ready=glyph.createSprite(16,20)!=nullptr; }
    if (!ready) return;
    const int n=(int)strlen(label);
    const float room=a.radius*(a.end-a.start)*StopWatchArc::RADIANS_PI/180 + 2*a.half-14;
    const int size=n*12 <= room ? 2 : 1;
    const float advance=6*size;
    const float mid=(a.start+a.end)*.5f;
    for (int i=0;i<n;++i) {
        const float offset=(i-(n-1)*.5f)*advance;
        const float angle=mid+(top ? -1 : 1)*offset/a.radius*180/StopWatchArc::RADIANS_PI;
        const float rotation=(top ? 180-angle : -angle)*StopWatchArc::RADIANS_PI/180;
        const float cs=cosf(rotation), sn=sinf(rotation);
        const auto p=StopWatchArc::point(a.radius,angle);
        glyph.fillRect(0,0,16,20,TFT_BLACK); glyph.setTextFont(1); glyph.setTextSize(size);
        glyph.setTextColor(TFT_WHITE,TFT_BLACK); glyph.setCursor(0,0); glyph.write((uint8_t)label[i]);
        for (int pass=shadow ? 0 : 1;pass<2;++pass)
        for (int y=0;y<8*size;++y) for (int x=0;x<6*size;++x) if (glyph.readPixel(x,y)!=TFT_BLACK) {
            const float dx=x-(6*size-1)*.5f, dy=y-(8*size-1)*.5f;
            const int px=lroundf(p.x+dx*cs-dy*sn), py=lroundf(p.y+dx*sn+dy*cs);
            if(pass) t.drawPixel(px,py,color);
            else { t.drawPixel(px-1,py,TFT_BLACK);t.drawPixel(px+1,py,TFT_BLACK);t.drawPixel(px,py-1,TFT_BLACK);t.drawPixel(px,py+1,TFT_BLACK); }
        }
    }
}
void endFrame(TFT_eSPI& t) {
    uint8_t indices[MAX], n=0;
    for (uint8_t i=0;i<count;++i) if (pending[i].rim) indices[n++]=i;
    for (uint8_t i=1;i<n;++i) for (uint8_t j=i;j>0;--j) {
        const auto& a=pending[indices[j-1]].box; const auto& b=pending[indices[j]].box;
        if (a.x+a.w/2 <= b.x+b.w/2) break;
        const uint8_t v=indices[j];indices[j]=indices[j-1];indices[j-1]=v;
    }
    for (uint8_t i=0;i<n;++i) {
        auto& c=pending[indices[i]]; c.arc=StopWatchArc::footer(i,n);
        drawArc(t,c.arc,c.pressed ? Theme::PURPLE : Theme::BG,Theme::CYAN);
        arcText(t,c.arc,c.label,Theme::WHITE);
    }
    visibleCount=count;
    for (uint8_t i=0;i<count;++i) visible[i]=pending[i];
    visibleContext=pendingContext;
}
uint8_t controls(Rect* out,uint8_t cap) {
    uint8_t n=0;
    for (uint8_t i=0;i<visibleCount && n<cap;++i) out[n++]=visible[i].box;
    return n;
}
bool touchPoint(uint8_t index,int& x,int& y) {
    if(index>=visibleCount) return false;
    const auto& c=visible[index];
    if(c.rim || c.custom) {
        const auto p=StopWatchArc::point(c.arc.radius,(c.arc.start+c.arc.end)*.5f);
        x=lroundf(p.x);y=lroundf(p.y);
    } else {x=c.box.x+c.box.w/2;y=c.box.y+c.box.h/2;}
    return true;
}
// Keep a rim press on its original logical target until physical release.
// Crossing an arc edge must not synthesize a release or a large list swipe.
bool touchSample(bool down,int& x,int& y) {
    static bool held=false, blocked=false, captured=false;
    static uint16_t ctx=0xffff;
    static int tx=0,ty=0;
    if(!down) {held=blocked=captured=false;return false;}
    if(held) {
        if(ctx!=visibleContext) blocked=true;
        if(blocked) return false;
        if(captured) {x=tx;y=ty;return true;}
        return true; // list gestures keep their real displacement
    }
    held=true;ctx=visibleContext;
    const int i=rimAt(x,y);
    if(i>=0) {
        tx=visible[i].box.x+visible[i].box.w/2;ty=visible[i].box.y+visible[i].box.h/2;
        captured=true;x=tx;y=ty;return true;
    }
    blocked=!mapTouch(x,y);
    return !blocked;
}
bool mapTouch(int& x,int& y) {
    const int hit=rimAt(x,y);
    if(hit>=0) {
        x=visible[hit].box.x+visible[hit].box.w/2;
        y=visible[hit].box.y+visible[hit].box.h/2;
        return true;
    }
    bool rim=false;
    for (uint8_t i=0;i<visibleCount;++i) if (visible[i].rim) {
        rim=true;
        if (StopWatchArc::contains(visible[i].arc,x,y)) {
            x=visible[i].box.x+visible[i].box.w/2;
            y=visible[i].box.y+visible[i].box.h/2;
            return true;
        }
    }
    // A normal button above the rim keeps its full touch region.
    for(uint8_t i=0;i<visibleCount;++i)
        if(!visible[i].rim && in(visible[i].box,x,y)) return true;
    // Gaps between rim controls are deliberately inert.
    return !(rim && y>=344);
}
bool focus(TFT_eSPI& t,int x,int y) {
    for (uint8_t i=0;i<visibleCount;++i) if (in(visible[i].box,x,y)) {
        const auto& c=visible[i];
        if(c.custom) { outlineArc(t,c.arc,Theme::WHITE); auto inner=c.arc;inner.half-=1;outlineArc(t,inner,Theme::CYAN); }
        else if (c.rim) { drawArc(t,c.arc,Theme::PURPLE,Theme::WHITE);arcText(t,c.arc,c.label,Theme::WHITE); }
        else { t.drawRoundRect(c.box.x-2,c.box.y-2,c.box.w+4,c.box.h+4,8,Theme::WHITE);t.drawRoundRect(c.box.x-3,c.box.y-3,c.box.w+6,c.box.h+6,9,Theme::CYAN); }
        return true;
    }
    if (StopWatchArc::contains(menuArc(),x,y)) {
        drawArc(t,menuArc(),Theme::PURPLE,Theme::WHITE);
        for (int dy=-9;dy<=9;dy+=9) t.fillRoundRect(216,(int)(232.5f-menuArc().radius)+dy,34,4,2,Theme::WHITE);
        return true;
    }
    return false;
}
void menu(TFT_eSPI& t) {
    drawArc(t,menuArc(),Theme::BG,Theme::CYAN);
    for (int dy=-9;dy<=9;dy+=9) t.fillRoundRect(216,(int)(232.5f-menuArc().radius)+dy,34,4,2,Theme::WHITE);
}
void holdProgress(TFT_eSPI& t,uint16_t left,uint16_t right) {
    for (int side=0;side<2;++side) {
        const uint16_t value=side ? right : left;
        if (!value) continue;
        const int cx=side ? 447 : 18, cy=233;
        t.fillCircle(cx,cy,13,Theme::BG);
        t.drawCircle(cx,cy,12,Theme::PURPLE);
        for (int a=0;a<=360*value/1000;a+=5) {
            const float r=a*StopWatchArc::RADIANS_PI/180;
            t.fillCircle(cx+(int)(11*sinf(r)),cy-(int)(11*cosf(r)),1,Theme::CYAN);
        }
    }
}
}
#endif
