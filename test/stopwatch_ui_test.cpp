// Link with the native simulator objects, excluding main_sim.o.
#include <stdio.h>
#include "stopwatch_ui.h"
#include "theme.h"
#include "settings.h"
#include "detection.h"
#include "ui_settings.h"
#include "ui_wifipass.h"
#include "ui_clear.h"
#include "ui_alert.h"
#include "ui_boot.h"
#include "ui_desk.h"
#include "squachy.h"
#include "stopwatch_geometry.h"
#include "ui_power.h"
#include "ui_light.h"
#include "ui_security.h"
#include "ui_detfilter.h"
#include "ui_meshmenu.h"
#include "ui_ignorelist.h"
#include "ignore_list.h"
#include "ui_wifinets.h"
#include "ui_myalerts.h"
#include "ui_update.h"
#include "ui_colorcheck.h"
#include "ui_squadupdate.h"
#include "ui_bingo.h"
#include "ui_dex.h"
#include "ui_phone.h"
#define SENSE_CYD_APP 1
#include "ui_sensing_cyd.h"
#undef SENSE_CYD_APP
static int failures=0, checks=0;
static void ck(const char* name,bool ok) {++checks;if(!ok){++failures;printf("FAIL: %s\n",name);}}
int main() {
    TFT_eSPI panel(466,466);panel.init();
    TFT_eSprite t(&panel);t.createSprite(466,466);
    Theme::setLarge(false);Theme::setCompact(false);Theme::setStillBackdrop(true);
    Settings::load();DetectionEngine eng;
    const auto bar=Theme::computeButtonBar(466,466);
    StopWatchUI::beginFrame(1);
    for(int i=0;i<3;++i)Theme::drawButton(t,bar.x[i],bar.y,bar.w[i],bar.h,"TEST",false);
    StopWatchUI::endFrame(t);
    bool mapped=true;
    for(int i=0;i<3;++i) {
        auto arc=StopWatchArc::footer(i,3);auto p=StopWatchArc::point(arc.radius,(arc.start+arc.end)/2);
        int x=(int)p.x,y=(int)p.y;
        mapped &= StopWatchUI::mapTouch(x,y) && x==bar.x[i]+bar.w[i]/2 && y==bar.y+bar.h/2;
    }
    ck("curved touch targets map to the existing handlers",mapped);
    int x=233,y=465;ck("space beyond the padded round button is inert",!StopWatchUI::mapTouch(x,y));
    x=233;y=200;ck("body touches are unchanged",StopWatchUI::mapTouch(x,y)&&x==233&&y==200);
    ck("focus finds the curved footer",StopWatchUI::focus(t,bar.x[1]+bar.w[1]/2,bar.y+bar.h/2));
    StopWatchUI::beginFrame(2);
    Theme::drawButton(t,100,398,90,30,"OLD",false);
    StopWatchUI::modal();Theme::drawButton(t,160,250,120,30,"CANCEL",false);StopWatchUI::endFrame(t);
    StopWatchUI::Rect controls[64];
    ck("modal removes obscured controls",StopWatchUI::controls(controls,64)==1 && controls[0].y==250);
    TFT_eSprite viewport(&t);viewport.createSprite(466,466);
    viewport.setViewport(40,0,386,466,true);ck("simulator matches TFT_eSprite viewport dimensions",viewport.width()==386 && viewport.height()==466);StopWatchUI::beginFrame(2);
    Theme::drawButton(viewport,10,180,80,30,"OFFSET",false);viewport.resetViewport();StopWatchUI::endFrame(viewport);
    ck("row viewport offset is included in focus geometry",StopWatchUI::controls(controls,64)==1 && controls[0].x==50);
    uiSettingsInit(t);uiSettingsOpenPage(SettingsPage::MAIN);
    auto draw=[&](){StopWatchUI::beginFrame(3);uiSettingsTick(t,100,eng);StopWatchUI::endFrame(t);};
    auto first=[&](){for(int y=72;y<340;++y){auto r=uiSettingsHitTest(t,233,y,466,466);if(r!=SettingsRow::NONE)return r;}return SettingsRow::NONE;};
    draw();ck("Settings Back remains available without a modal",StopWatchUI::controls(controls,64)>=1);const auto initial=first();
    uiSettingsScroll(5);draw();const auto scrolled=first();
    ck("scroll exposes additional menu items",initial!=scrolled && scrolled!=SettingsRow::NONE);
    uiSettingsOpenPage(SettingsPage::APPEARANCE);draw();uiSettingsScroll(3);draw();
    uiSettingsOpenPage(SettingsPage::MAIN);draw();
    ck("parent menu retains scroll after a nested page",first()==scrolled);
    uiSettingsInit(t);draw();ck("return from another screen retains scroll",first()==scrolled);
    uiSettingsOpenPage(SettingsPage::DESK);draw();
    ck("Desk settings expose both OK and UP",StopWatchUI::controls(controls,64)>=2);
    uiSettingsOpenPage(SettingsPage::FONTS);draw();
    bool minus=false,plus=false;
    for(int y=72;y<340;y+=2)for(int x=8;x<458;x+=4) {
        const int part=uiSettingsFontFocusPart(t,x,y,466,466);minus|=part==2;plus|=part==3;
    }
    ck("font sliders expose separate enabled minus and plus controls",minus && plus);
    bool touchRows=true, stepButtons=true;int stepCount=0;
    for(int page=0;page<=5;++page) {
        uiSettingsOpenPage((SettingsPage)page);uiSettingsScroll(-100);draw();
        for(int scroll=0;scroll<40;++scroll) {
            draw();
            for(int yy=76;yy<338;yy+=7) {
                t.setTextFont(1);auto expected=uiSettingsHitTest(t,233,yy,466,466);
                t.setTextFont(2);touchRows &= expected==uiSettingsHitTest(t,233,yy,466,466);
            }
            int n=StopWatchUI::controls(controls,64);
            for(int i=0;i<n;++i) {
                int xx=controls[i].x+controls[i].w/2, yy=controls[i].y+controls[i].h/2;
                auto row=uiSettingsHitTest(t,xx,yy,466,466);
                if(row==SettingsRow::BRIGHTNESS) {
                    ++stepCount;const int hit=uiSettingsStepHit(t,xx,yy,466,466);if(!hit)printf("miss brightness page=%d x=%d y=%d w=%d h=%d\n",page,xx,yy,controls[i].w,controls[i].h);stepButtons &= hit!=0;
                }
            }
            uiSettingsScroll(1);
        }
    }
    ck("submenu touch rows do not depend on previous font",touchRows);
    ck("drawn brightness buttons hit their step handlers",stepCount>0 && stepButtons);
    StopWatchUI::beginFrame(0);
    const StopWatchArc::Arc counter={212,170,190,13};
    StopWatchUI::arcControl(counter,"TAG 4");StopWatchUI::endFrame(t);
    ck("device counters register curved focus",StopWatchUI::focus(t,233,20));
    // Every drawn control must map to the same handler as its logical box.
    auto audit=[&](const char* name,auto render,auto hit) {
        StopWatchUI::beginFrame(300);render();StopWatchUI::endFrame(t);
        const int n=StopWatchUI::controls(controls,64);bool good=n>0;
        for(int i=0;i<n;++i) {
            int px,py;StopWatchUI::touchPoint(i,px,py);
            const int expected=hit(controls[i].x+controls[i].w/2,controls[i].y+controls[i].h/2);
            good &= expected!=0 && StopWatchUI::mapTouch(px,py) && hit(px,py)==expected;
            if(!expected)printf("unhandled %s control %d at %d,%d\n",name,i,px,py);
        }
        ck(name,good);
    };
    auto listAudit=[&](const char* name,auto init,auto render,auto hit,auto scroll) {
        init();bool good=true;
        for(int offset=0;offset<15;++offset) {
            StopWatchUI::beginFrame(300);render();StopWatchUI::endFrame(t);
            for(int yy=72;yy<340;yy+=3) {
                t.setTextFont(1);const int a=(int)hit(233,yy);
                t.setTextFont(2);good &= a==(int)hit(233,yy);
            }
            scroll();
        }
        ck(name,good);
    };
    listAudit("Power rows stable across scroll and font",[&](){uiPowerInit(t);},[&](){uiPowerTick(t,100,eng);},[&](int x,int y){return uiPowerHitTest(t,x,y,466,466);},[&](){uiPowerScroll(1);});
    listAudit("Light rows stable across scroll and font",[&](){uiLightInit(t);},[&](){uiLightTick(t,100,eng);},[&](int x,int y){return uiLightHitTest(t,x,y,466,466);},[&](){uiLightScroll(1);});
    listAudit("Security rows stable across scroll and font",[&](){uiSecurityInit(t);},[&](){uiSecurityTick(t,100,eng);},[&](int x,int y){return uiSecurityHitTest(t,x,y,466,466);},[&](){uiSecurityScroll(1);});
    listAudit("Detection filter rows stable across scroll and font",[&](){uiDetFilterInit(t);},[&](){uiDetFilterTick(t,100,eng);},[&](int x,int y){return uiDetFilterHitTest(t,x,y,466,466);},[&](){uiDetFilterScroll(1);});
    listAudit("Mesh menu rows stable across scroll and font",[&](){uiMeshMenuInit(t);},[&](){uiMeshMenuTick(t,100,eng);},[&](int x,int y){return uiMeshMenuHitTest(t,x,y,466,466);},[&](){uiMeshMenuScroll(1);});
    IgnoreList::clear();
    for(int i=0;i<12;++i){uint8_t mac[6]={2,0,0,0,0,(uint8_t)(i+1)};IgnoreList::add(mac,DetectionType::AIRTAG);}
    uiIgnoreListInit(t);
    for(int offset=0;offset<10;++offset) {
        audit("Ignored device Remove and Back touch",[&](){uiIgnoreListTick(t,100);},[&](int x,int y){return Theme::pinnedBackHit(x,y,466,466)?1:uiIgnoreListHitRemove(t,x,y,466,466)!=0xff?2:0;});
        uiIgnoreListScroll(1);
    }
    IgnoreList::clear();
    uiWifiNetsInit(t);audit("Saved WiFi footer touch",[&](){uiWifiNetsTick(t,100);},[&](int x,int y){return (int)uiWifiNetsHit(t,x,y,nullptr);});
    uiWifiAddInit(t);audit("WiFi scan footer touch",[&](){uiWifiAddTick(t,100,eng);},[&](int x,int y){return (int)uiWifiAddHit(t,x,y,eng,nullptr);});
    uiMyAlertsInit(t);audit("My Alerts footer touch",[&](){uiMyAlertsTick(t,100);},[&](int x,int y){return (int)uiMyAlertsHit(t,x,y,nullptr);});
    uiUpdateInit(t);audit("Firmware update menu touch",[&](){uiUpdateTick(t,100);},[&](int x,int y){return (int)uiUpdateHitTest(t,x,y,nullptr);});
    uiUpdateAskSwitch(true);audit("Firmware switch confirmation touch",[&](){uiUpdateTick(t,100);},[&](int x,int y){return (int)uiUpdateHitTest(t,x,y,nullptr);});uiUpdateAskSwitch(false);
    uiColorCheckInit(t);audit("Color-check buttons touch",[&](){uiColorCheckTick(t,100);},[&](int x,int y){return (int)uiColorCheckHitTest(x,y,466,466);});
    uiSquadUpdateInit(t);audit("Squad update buttons touch",[&](){uiSquadUpdateTick(t,100,eng);},[&](int x,int y){return (int)uiSquadUpdateHit(t,x,y);});
    StopWatchUI::beginFrame(301);Theme::drawButton(t,150,398,166,38,"BACK",false);StopWatchUI::endFrame(t);
    int px=233,py=426;StopWatchUI::touchSample(false,px,py);
    bool capture=StopWatchUI::touchSample(true,px,py);const int tx=px,ty=py;
    px=40;py=350;capture &= StopWatchUI::touchSample(true,px,py) && px==tx && py==ty;
    ck("rim press remains stable across arc edge",capture);
    StopWatchUI::touchSample(false,px,py);px=233;py=465;
    bool gap=!StopWatchUI::touchSample(true,px,py);px=233;py=426;gap &= !StopWatchUI::touchSample(true,px,py);
    ck("footer-gap press remains consumed until release",gap);StopWatchUI::touchSample(false,px,py);
    for(auto row:{SettingsRow::CALIBRATE,SettingsRow::RESET_STATS,SettingsRow::REPLAY_INTRO}) {
        uiSettingsSetConfirm(row);
        audit("Settings confirmation buttons only",[&](){uiSettingsTick(t,100,eng);},[&](int x,int y){return (int)uiSettingsConfirmButtonAt(x,y,466,466);});
        ck("settings dialog excludes underlying controls",StopWatchUI::controls(controls,64)==2);
    }
    uiSettingsSetConfirm(SettingsRow::NONE);
    uiBingoInit(t);uiBingoHitTest(t,bar.x[1]+bar.w[1]/2,bar.y+bar.h/2,466,466);
    StopWatchUI::beginFrame(300);uiBingoTick(t,100,eng);StopWatchUI::endFrame(t);
    ck("Bingo confirmation hides underlying footer targets",StopWatchUI::controls(controls,64)==2);
    // The real keyboard used to stop registering its footer after settling.
    uiWifiPassInit(t,"test");
    StopWatchUI::beginFrame(300);uiWifiPassTick(t,100);StopWatchUI::endFrame(t);
    const int initialKeys=StopWatchUI::controls(controls,64);
    StopWatchUI::beginFrame(300);uiWifiPassTick(t,100000);StopWatchUI::endFrame(t);
    ck("idle password keyboard retains all touch controls",initialKeys>0 && StopWatchUI::controls(controls,64)==initialKeys);
    int16_t backX,backY,backW,backH;uiWifiPassBackRect(backX,backY,backW,backH);
    bool backWorks=false;
    for(int i=0;i<initialKeys;++i) if(controls[i].x==backX && controls[i].y==backY) {
        int x,y;StopWatchUI::touchPoint(i,x,y);StopWatchUI::mapTouch(x,y);
        uiWifiPassTouch(x,y,100100,WifiPassTouch::DOWN);uiWifiPassTouch(x,y,100150,WifiPassTouch::UP);
        backWorks=uiWifiPassResult()==WifiPassResult::BACK;
    }
    ck("idle password keyboard Back activates through curved touch",backWorks);
    uiWifiPassInit(t,"test");uiWifiPassTick(t,100);
    int16_t rx[64],ry[64],rw[64],rh[64];
    const int empty=uiWifiPassStopRects(rx,ry,rw,rh,64);
    uiWifiPassKey('a',200);uiWifiPassTick(t,201);
    ck("disabled empty-password OK is excluded from focus",uiWifiPassStopRects(rx,ry,rw,rh,64)==empty+1);
    // Exercise every printable character using the rendered paged keyboard.
    auto keyboardDraw=[&](){StopWatchUI::beginFrame(300);uiWifiPassTick(t,200000);StopWatchUI::endFrame(t);};
    uint32_t keyTime=200000;
    auto keyTap=[&](int idx){
        int16_t ax[64],ay[64],aw[64],ah[64];uiWifiPassStopRects(ax,ay,aw,ah,64);
        int x=ax[idx]+aw[idx]/2,y=ay[idx]+ah[idx]/2;
        uiWifiPassTouch(x,y,keyTime+=100,WifiPassTouch::DOWN);
        uiWifiPassTouch(x,y,keyTime+=100,WifiPassTouch::UP);keyboardDraw();
    };
    const char* groups[]={"abcdefghijklmnopqrstuvwxyz ","1234567890 ","!@#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~ "};
    bool allChars=true,largeKeys=true;
    for(int group=0;group<3;++group) {
        uiWifiPassInit(t,"test");keyboardDraw();keyTap(2+group);
        const int len=strlen(groups[group]);
        for(int i=0;i<len;++i) {
            if(i && i%15==0) keyTap(5);
            int16_t ax[64],ay[64],aw[64],ah[64];uiWifiPassStopRects(ax,ay,aw,ah,64);
            largeKeys &= aw[6+i%15]>=44 && ah[6+i%15]>=44;
            keyTap(6+i%15);
        }
        allChars &= strcmp(uiWifiPassText(),groups[group])==0;
    }
    ck("paged keyboard types all letters digits symbols and spaces",allChars);
    ck("character targets are at least 44 pixels square",largeKeys);
    uiWifiPassInit(t,"test");keyboardDraw();
    keyTap(21); // SHIFT, following four tabs and fifteen letters.
    keyTap(6);keyTap(22); // A, then DEL.
    ck("large keyboard Shift and Delete remain functional",uiWifiPassText()[0]==0);
    keyTap(6);keyTap(23);
    ck("large keyboard confirms uppercase password",strcmp(uiWifiPassText(),"A")==0 && uiWifiPassResult()==WifiPassResult::OK);
    StopWatchUI::beginFrame(301);Theme::drawButton(t,150,398,166,38,"BACK",false);StopWatchUI::endFrame(t);
    int ex=233,ey=445;ck("rim accepts a five-pixel near miss",StopWatchUI::mapTouch(ex,ey) && ey==417);
    eng.init();
    for(int i=0;i<8;++i) {
        Detection d{};d.mac[5]=i+1;d.type=DetectionType::AIRTAG;d.rssi=-45-i;
        d.active=true;d.hits=1;snprintf(d.name,sizeof d.name,"Device %d",i+1);eng.postBle(d);
    }
    if(!Settings::boringMode())Settings::toggleBoringMode();
    uiClearInit(t);SimClock::virtualTime=true;SimClock::nowMs=10000;
    auto home=[&](){StopWatchUI::beginFrame((uint16_t)AppState::CLEAR*64);uiClearTick(t,SimClock::nowMs,eng,true,false);StopWatchUI::endFrame(t);};
    home();int targets=uiTerminalStops(rx,ry,rw,rh,64);int rows=0;bool onGlass=true;
    for(int i=0;i<targets;++i) {
        if(rh[i]==38)++rows;
        onGlass &= StopWatchGeometry::visible(rx[i],ry[i]) && StopWatchGeometry::visible(rx[i]+rw[i]-1,ry[i]+rh[i]-1);
    }
    ck("terminal shows five large device rows",rows==5);
    ck("terminal focus targets stay inside the circle",onGlass);
    auto tap=[&](int x,int y){uiTerminalTouch(x,y,true,true,SimClock::nowMs++);uiTerminalTouch(x,y,false,false,SimClock::nowMs++);};
    tap(30,234);home();
    targets=uiTerminalStops(rx,ry,rw,rh,64);bool forward=false;
    for(int i=0;i<targets;++i)if(rx[i]==418 && rw[i]==36)forward=true;
    ck("terminal scrolling enables return toward latest",forward);
    tap(233,130);tap(233,130);uint8_t opened[6];
    ck("terminal row selection still opens device details",uiTerminalTakeOpen(opened));
    Settings::toggleBoringMode();uiClearInit(t);
    Squachy::announce("Speech stays below the found devices.");
    for(int i=0;i<20;++i){SimClock::nowMs+=40;home();}
    int16_t counterX[16],counterY[16];
    const int counterN=uiClearCounterStops(counterX,counterY,16);
    bool curved=counterN>0;
    for(int i=0;i<counterN;++i) {
        DetectionType type;
        curved &= uiClearCounterHit(counterX[i],counterY[i],type)
            && StopWatchUI::focus(t,counterX[i],counterY[i])
            && StopWatchGeometry::visible(counterX[i],counterY[i]);
    }
    ck("rendered rim counters share touch and focus targets",curved);
    ck("home menu sits beneath the outer counters",Theme::settingsButtonHit(233,120) && !Theme::settingsButtonHit(233,20));
    int bx,by,bw,bh;
    ck("avatar speech stays below the badge rail",Squachy::ownBubble(bx,by,bw,bh) && by>=212);
    uiSensingInit(t);
    StopWatchUI::beginFrame((uint16_t)AppState::SENSING*64);uiSensingTick(t,100000);StopWatchUI::endFrame(t);
    ck("sensing WiFi card responds above the footer",uiSensingTap(233,313,466,466,100010)==SensingTap::ADD_WIFI);
    int wx=233,wy=313;ck("WiFi card remains reachable through touch mapping",StopWatchUI::mapTouch(wx,wy) && wx==233 && wy==313);
    int16_t sx,sy;uiSensingBannerStop(466,466,sx,sy);
    ck("sensing WiFi card clears the footer",sy+21<344);
    ck("sensing banner has a full-card focus outline",StopWatchUI::focus(t,sx,sy));
    uiSettingsOpenPage(SettingsPage::MAIN);
    StopWatchUI::beginFrame((uint16_t)AppState::SETTINGS*64);uiSettingsTick(t,100020,eng);StopWatchUI::endFrame(t);
    ck("Settings erases previous sensing side buttons",t.readPixel(115,387)==Theme::BG && t.readPixel(350,387)==Theme::BG);
    // A handler switches screen after drawing: its init clears the sprite,
    // but that incomplete frame must not reach the panel or show old focus.
    StopWatchUI::beginFrame(400);Theme::drawButton(t,150,398,166,38,"OLD",false);
    ck("screen change discards incomplete outgoing frame",!StopWatchUI::endFrameIfCurrent(t,401));
    ck("screen change discards outgoing touch controls",StopWatchUI::controls(controls,64)==0);
    ck("transition cannot display stale focus",!StopWatchUI::focusReady(401,400));
    StopWatchUI::beginFrame(401);Theme::drawButton(t,150,398,166,38,"NEW",false);
    ck("complete incoming frame can be presented",StopWatchUI::endFrameIfCurrent(t,401));
    ck("new frame waits for navigation focus restoration",!StopWatchUI::focusReady(401,400));
    ck("restored incoming focus is visible",StopWatchUI::focusReady(401,401));
    ck("incoming focus follows its curved outline",StopWatchUI::focus(t,233,417));
    Settings::previewBackground(Settings::Background::BLACK);
    Detection alert{};alert.type=DetectionType::AIRTAG;alert.rssi=-60;alert.hits=1;
    uiAlertInit(t,alert);StopWatchUI::beginFrame((uint16_t)AppState::ALERT*64);
    uiAlertTick(t,100030,eng,false,nullptr,nullptr);StopWatchUI::endFrame(t);
    ck("device-found screen has no outer colored ring",t.readPixel(233,459)==Theme::BG && t.readPixel(7,233)==Theme::BG);
    for(int stage=0;stage<2;++stage) {
        const uint16_t color=stage ? Theme::AMBER:Theme::CYAN;
        uiBootStatus(t,stage ? "TOUCH CALIBRATION":"CHECKING UPDATES",
                     stage ? "Hold anywhere now":"Connecting to WiFi",
                     stage ? "to reset calibration":"Please wait...",color);
        int titlePixels=0,detailPixels=0;bool safe=true;
        for(int y=0;y<466;++y)for(int x=0;x<466;++x) if(t.readPixel(x,y)!=Theme::BLACK) {
            safe &= StopWatchGeometry::visible(x,y) && x>=60 && x<406 && y>=180 && y<274;
            if(y<210)++titlePixels;else ++detailPixels;
        }
        ck("early boot prompt is large and inside round glass",safe && titlePixels>1000 && detailPixels>500);
    }
    uiDeskInit(t);uiDeskAlert(alert,100000);
    StopWatchUI::beginFrame(700);uiDeskTick(t,100100,eng,false);StopWatchUI::endFrame(t);
    ck("desktop notification tap opens the visible card",uiDeskHitAlert(233,304,100100));
    ck("old clipped notification target is gone",!uiDeskHitAlert(10,320,100100));
    ck("desktop notification focus uses full card",StopWatchUI::focus(t,233,304));
    ck("desktop notification focus stays inside glass",StopWatchGeometry::visible(80,269)&&StopWatchGeometry::visible(386,339));
    StopWatchUI::beginFrame(700);uiDeskTick(t,110000,eng,false);StopWatchUI::endFrame(t);
    ck("expired desktop notification loses tap and focus",!uiDeskHitAlert(233,304,110000)&&!StopWatchUI::focus(t,233,304));
    printf("%d UI checks, %d failures\n",checks,failures);return failures?1:0;
}
