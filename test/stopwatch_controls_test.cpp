#include <stdio.h>
#include "stopwatch_input.h"
#include "stopwatch_arc.h"
using namespace StopWatchInput;
static int failures=0, checks=0;
static void ck(const char* name,bool ok) { ++checks; if(!ok) {++failures;printf("FAIL: %s\n",name);} }
int main() {
    Controller c;
    c.tick(false,false,0,false,false);
    ck("right down does not advance",c.tick(false,true,10,false,false)==NONE);
    ck("499 ms is still a short press",c.tick(false,true,509,false,false)==NONE);
    ck("short release advances",c.tick(false,false,509,false,false)==NEXT);
    c.tick(false,true,600,false,false);
    ck("500 ms activates",c.tick(false,true,1100,false,false)==SELECT);
    ck("hold fires once",c.tick(false,true,2000,false,false)==NONE);
    ck("release after hold does nothing",c.tick(false,false,2100,false,false)==NONE);
    c.tick(true,false,2200,false,false);
    ck("left short goes back",c.tick(false,false,2300,false,false)==BACK);
    c.tick(true,false,2400,false,false);
    ck("release after missed threshold still goes home",c.tick(false,false,3000,false,false)==HOME);
    c.tick(false,true,3100,false,false);
    ck("hold progress halfway",c.right.progress(3350)==500);
    c.tick(false,false,3200,false,false);
    ck("dim press only wakes",c.tick(false,true,4000,true,false)==WAKE);
    ck("waking hold consumed",c.tick(false,true,5000,false,false)==NONE);
    ck("waking release consumed",c.tick(false,false,5100,false,false)==NONE);
    c.tick(false,true,5200,false,false);
    ck("next complete press works",c.tick(false,false,5300,false,false)==NEXT);
    c.tick(true,false,5400,false,false);
    ck("update consumes an in-flight press",c.tick(true,false,5600,false,true)==NONE);
    ck("update press cannot activate later",c.tick(true,false,6100,false,false)==NONE);
    ck("update release consumed",c.tick(false,false,6200,false,false)==NONE);
    Controller boot;
    ck("held at startup consumed",boot.tick(true,true,0,false,false)==NONE);
    ck("startup hold never fires",boot.tick(true,true,1000,false,false)==NONE);
    ck("startup release never clicks",boot.tick(false,false,1100,false,false)==NONE);
    c.tick(false,true,0xffffff00u,false,false);
    ck("millis wrap supports hold",c.tick(false,true,0x100u,false,false)==SELECT);
    ck("wrap release no duplicate",c.tick(false,false,0x200u,false,false)==NONE);
    c.tick(false,true,1000,false,false);
    ck("contact bounce ignored",c.tick(false,false,1010,false,false)==NONE);
    ck("last focus wraps to first",nextFocus(4,5)==0);
    ck("focus advances exactly one",nextFocus(2,5)==3);
    ck("empty and single-item focus are stable",nextFocus(0,0)==0 && nextFocus(0,1)==0);
    Controller both;
    both.tick(false,false,0,false,false);
    ck("both down emits no individual action",both.tick(true,true,10,false,false)==NONE);
    ck("first release waits for the other button",both.tick(false,true,110,false,false)==NONE);
    ck("short chord opens menu on final release",both.tick(false,false,150,false,false)==MENU);
    both.tick(true,false,200,false,false);
    ck("staggered chord claims first button",both.tick(true,true,400,false,false)==NONE);
    ck("hold threshold starts when both are down",both.tick(true,true,899,false,false)==NONE);
    ck("both hold indicators show the chord",both.progress(false,650)==500 && both.progress(true,650)==500);
    ck("both held for 500 ms powers off",both.tick(true,true,900,false,false)==POWER_OFF);
    ck("power off fires only once",both.tick(true,true,1500,false,false)==NONE);
    ck("long chord first release is consumed",both.tick(true,false,1600,false,false)==NONE);
    ck("long chord final release is consumed",both.tick(false,false,1700,false,false)==NONE);
    both.tick(true,true,1800,false,false);both.tick(false,true,1900,false,false);
    ck("one remaining button cannot turn a short chord into power off",both.tick(false,true,2800,false,false)==NONE);
    ck("short chord survives staggered release",both.tick(false,false,2900,false,false)==MENU);
    ck("dimmed chord only wakes",both.tick(true,true,3000,true,false)==WAKE);
    ck("wake chord cannot power off",both.tick(true,true,3600,false,false)==NONE);
    ck("wake chord cannot open menu",both.tick(false,false,3700,false,false)==NONE);
    both.tick(true,true,3800,false,false);both.tick(true,true,3900,false,true);
    ck("update blocks in-flight chord",both.tick(true,true,4400,false,false)==NONE);
    ck("blocked chord release consumed",both.tick(false,false,4500,false,false)==NONE);
    both.tick(true,true,4600,false,false);both.consume();
    ck("touch takeover consumes chord",both.tick(true,true,5200,false,false)==NONE);
    both.tick(false,false,5300,false,false);
    both.tick(true,false,5400,false,false);both.tick(true,false,5900,false,false);
    ck("joining an already-fired single cannot power off",both.tick(true,true,6000,false,false)==NONE && both.tick(true,true,6600,false,false)==NONE);
    both.tick(false,false,6700,false,false);
    both.tick(true,true,6800,false,false);
    ck("release after missed chord threshold powers off once",both.tick(false,false,7400,false,false)==POWER_OFF);
    both.tick(true,true,0xffffff00u,false,false);
    ck("chord timer survives millis wrap",both.tick(true,true,0x100u,false,false)==POWER_OFF);
    both.tick(false,false,0x200u,false,false);
    both.tick(false,true,8000,false,false);
    ck("individual controls work after a chord",both.tick(false,false,8100,false,false)==NEXT);
    FocusMemory memory;
    int parent[]={10,20,30}, nested[]={40,50}, changed[]={30,10,20}, removed[]={10};
    memory.save(64,20,1);memory.save(65,50,1);
    ck("parent focus remembered after nested menu",memory.restore(64,parent,3)==1);
    ck("nested focus remembered independently",memory.restore(65,nested,2)==1);
    ck("focus follows stable item after reorder",memory.restore(64,changed,3)==2);
    ck("removed item has valid fallback",memory.restore(64,removed,1)==0);
    ck("empty screen resets focus",memory.restore(64,nullptr,0)==0);
    ck("new screen starts at first item",memory.restore(66,parent,3)==0);
    bool inside=true,separate=true,centers=true,clearance=true,belowContent=true;
    for(int n=1;n<=4;++n) {
        for(int i=0;i<n;++i) {
            auto a=StopWatchArc::footer(i,n);auto p=StopWatchArc::point(a.radius,(a.start+a.end)/2);
            centers &= StopWatchArc::contains(a,p.x,p.y);
        }
        for(int y=0;y<466;++y)for(int x=0;x<466;++x) {
            int hits=0;
            for(int i=0;i<n;++i) hits+=StopWatchArc::contains(StopWatchArc::footer(i,n),x,y);
            separate &= hits<=1;
            if(hits) {
                clearance &= (x-232.5f)*(x-232.5f)+(y-232.5f)*(y-232.5f)<=209*209;
                belowContent &= y>=344;
            }
            if(hits || StopWatchArc::contains(StopWatchArc::menu(),x,y))
                inside &= (x-232.5f)*(x-232.5f)+(y-232.5f)*(y-232.5f)<233*233;
        }
    }
    ck("all arc touch targets stay inside glass",inside);
    ck("rounded footer targets never overlap",separate);
    ck("every footer center is clickable",centers);
    ck("footer outlines leave at least 24 pixels to the glass edge",clearance);
    ck("inset footer controls stay below the content",belowContent);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
