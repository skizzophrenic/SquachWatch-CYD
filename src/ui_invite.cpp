// SquachWatch-CYD — the ADD TO SQUAD screen. See ui_invite.h.
#if SQUACH_MESH
#include "ui_invite.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
using MeshTalk::InviteState;

int btnH() { return Theme::dialogBtnH(28); }
const int SLOP  = 6;

bool        s_demo = false;
InviteState s_demoState = InviteState::IDLE;
uint16_t    s_demoCode = 0;
char        s_demoName[13] = "";
bool        s_demoInviter = false;
bool        s_showPhrase = false;

InviteState st()      { return s_demo ? s_demoState : MeshTalk::inviteState(); }
uint16_t    code()    { return s_demo ? s_demoCode : MeshTalk::inviteCode(); }
const char* peer()    { return s_demo ? s_demoName : MeshTalk::invitePeerName(); }
bool        inviter() { return s_demo ? s_demoInviter : MeshTalk::inviteIsInviter(); }

struct Btns { int y, w, leftX, rightX, oneX; };

Btns btns(TFT_eSPI& t) {
    Btns b;
    b.w = Theme::large() ? 140 : 110;
    const int gap = 14;
    b.leftX  = (t.width() - (b.w * 2 + gap)) / 2;
    b.rightX = b.leftX + b.w + gap;
    b.oneX   = (t.width() - b.w) / 2;
    b.y      = t.height() - btnH() -
#if defined(M5_STOPWATCH)
48;
#else
8;
#endif
    return b;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x <= bx + bw + SLOP && y >= by - SLOP && y <= by + bh + SLOP;
}

void centred(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor((t.width() - t.textWidth(s)) / 2, y);
    t.print(s);
}

// Which buttons the current page has: two, one, or none, and their labels.
struct Page { const char* left; const char* right; const char* one; };

Page page() {
    const bool inv = inviter();
    switch (st()) {
        case InviteState::OFFERING: return { nullptr, nullptr, "CANCEL" };
        case InviteState::ASKED:    return { "ACCEPT", "DECLINE", nullptr };
        case InviteState::CODE:     return { "MATCHES", "NO", nullptr };
        case InviteState::SENDING:  return { nullptr, nullptr, "DONE" };
        case InviteState::WAITING:  return { nullptr, nullptr, "CANCEL" };
        case InviteState::JOINED:   return { nullptr, nullptr, "OK" };
        case InviteState::DONE:     return { nullptr, nullptr, "BACK" };
        case InviteState::FAILED:
            if (s_showPhrase)          return { nullptr, nullptr, "BACK" };
            return (inv && Settings::phraseShown()) ? Page{ "SHOW PHRASE", "BACK", nullptr } : Page{ nullptr, nullptr, "BACK" };
        default:                    return { nullptr, nullptr, "BACK" };
    }
}
} // namespace

void uiInviteDemo(InviteState s, uint16_t c, const char* name, bool inv) {
    s_demo = true; s_demoState = s; s_demoCode = c; s_demoInviter = inv;
    snprintf(s_demoName, sizeof s_demoName, "%s", name ? name : "");
}

void uiInviteInit(TFT_eSPI& t) {
    s_demo = false;
    s_showPhrase = false;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiInviteShowingPhrase() { return s_showPhrase; }

void uiInviteTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)eng;
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, "ADD TO SQUAD", Theme::VAPOR_PINK);
    Theme::bodyFontOn(t);
    int y = Theme::LIST_TOP + Theme::listHeadingH() + 10;
    char line[48];
    const char* who = peer()[0] ? peer() : "SOMEONE";
    const Btns b = btns(t);
    const int midY = y + Theme::bodyY(36) + (b.y - (y + Theme::bodyY(36)) - 16) / 2;   // where a big line sits

    switch (st()) {
        case InviteState::OFFERING:
            snprintf(line, sizeof line, "Asking %s's board.", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + Theme::bodyY(12), Theme::W95_LIGHT, "It will ask them to accept.");
            centred(t, y + Theme::bodyY(24), Theme::W95_LIGHT, "Keep the boards close.");
            {
                // A slow dot march, so a still screen still reads as waiting.
                const int n = (int)((now / 400) % 4);
                char dots[8] = "";
                for (int i = 0; i < n; i++) dots[i] = '.';
                t.setTextFont(1); t.setTextSize(Theme::upSize(2));
                centred(t, midY, Theme::CYAN, dots[0] ? dots : " ");
                Theme::bodyFontOn(t);
            }
            break;
        case InviteState::ASKED:
            snprintf(line, sizeof line, "%s wants to add you", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + Theme::bodyY(12), Theme::WHITE, "to their squad.");
            centred(t, y + Theme::bodyY(30), Theme::W95_LIGHT, "You'd get their phrase: their");
            centred(t, y + Theme::bodyY(42), Theme::W95_LIGHT, "messages, visits and updates.");
            centred(t, y + Theme::bodyY(54), Theme::W95_LIGHT, "Only if you know who this is.");
            break;
        case InviteState::CODE: {
            centred(t, y, Theme::WHITE, "Both boards show four digits.");
            centred(t, y + Theme::bodyY(12), Theme::WHITE, "Read yours out. Do they match?");
            t.setTextFont(1); t.setTextSize(Theme::upSize(3));
            snprintf(line, sizeof line, "%04u", (unsigned)code());
            centred(t, midY - 6, Theme::CYAN, line);
            Theme::bodyFontOn(t);
            centred(t, b.y - t.fontHeight() - 8, Theme::W95_LIGHT, "Different digits means somebody is in the middle.");
            break;
        }
        case InviteState::SENDING:
            snprintf(line, sizeof line, "Sending the phrase to %s.", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + Theme::bodyY(12), Theme::W95_LIGHT, "Their board says when it has it.");
            centred(t, y + Theme::bodyY(24), Theme::W95_LIGHT, "Half a minute at most.");
            break;
        case InviteState::WAITING:
            centred(t, y, Theme::WHITE, "Waiting for the phrase.");
            snprintf(line, sizeof line, "%s's board is sending it.", who);
            centred(t, y + Theme::bodyY(12), Theme::W95_LIGHT, line);
            break;
        case InviteState::JOINED:
            snprintf(line, sizeof line, "You're in %s's squad.", who);
            t.setTextFont(1); t.setTextSize(Theme::upSize(2));
            centred(t, midY - 8, Theme::GREEN, "YOU'RE IN");
            Theme::bodyFontOn(t);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + Theme::bodyY(12), Theme::W95_LIGHT, "Messages and visits are on.");
            break;
        case InviteState::DONE:
            if (MeshTalk::inviteConfirmed()) {
                snprintf(line, sizeof line, "%s is in your squad.", who);
                t.setTextFont(1); t.setTextSize(Theme::upSize(2));
                centred(t, midY - 8, Theme::GREEN, "ADDED");
                Theme::bodyFontOn(t);
                centred(t, y, Theme::WHITE, line);
                centred(t, y + Theme::bodyY(12), Theme::W95_LIGHT, "Messages and visits are on.");
            } else {
                snprintf(line, sizeof line, "The phrase went out to %s,", who);
                centred(t, y, Theme::WHITE, line);
                centred(t, y + Theme::bodyY(12), Theme::WHITE, "but their board has not answered.");
                centred(t, y + Theme::bodyY(24), Theme::W95_LIGHT, "Check their screen. If it did not");
                centred(t, y + Theme::bodyY(36), Theme::W95_LIGHT, "take, SHOW PHRASE and read it out.");
            }
            break;
        case InviteState::FAILED:
            if (s_showPhrase) {
                centred(t, y, Theme::WHITE, "Read this out. They type it");
                centred(t, y + Theme::bodyY(12), Theme::WHITE, "under SQUACHMESH, PHRASE.");
                // Five words on two lines, big enough to read across a table.
                {
                    const char* p = MeshTalk::phrase();
                    char l1[24] = "", l2[24] = "";
                    const size_t n = strlen(p);
                    size_t cut = n;
                    if (n > 20) { cut = 20; while (cut > 0 && p[cut] != ' ') cut--; if (cut == 0) cut = 20; }
                    snprintf(l1, sizeof l1, "%.*s", (int)cut, p);
                    snprintf(l2, sizeof l2, "%s", p + cut + (p[cut] == ' ' ? 1 : 0));
                    // A size up on the large panels, if both lines still fit.
                    t.setTextFont(1); t.setTextSize(Theme::upSize(2));
                    if (t.textWidth(l1) > w - 16 || t.textWidth(l2) > w - 16) t.setTextSize(2);
                    centred(t, midY - 12, Theme::CYAN, l1);
                    if (l2[0]) centred(t, midY - 12 + t.fontHeight() + 4, Theme::CYAN, l2);
                    Theme::bodyFontOn(t);
                }
            } else {
                centred(t, y, Theme::AMBER, "That didn't work.");
                centred(t, y + Theme::bodyY(12), Theme::WHITE, MeshTalk::inviteWhy());
                if (inviter() && Settings::phraseShown()) {
                    centred(t, y + Theme::bodyY(30), Theme::W95_LIGHT, "The sure way: show them the phrase");
                    centred(t, y + Theme::bodyY(42), Theme::W95_LIGHT, "and let them type it.");
                } else if (inviter()) {
                    centred(t, y + Theme::bodyY(30), Theme::W95_LIGHT, "This board keeps its phrase hidden.");
                    centred(t, y + Theme::bodyY(42), Theme::W95_LIGHT, "Move closer and try again.");
                }
            }
            break;
        default:
            centred(t, y, Theme::W95_LIGHT, "Nothing going on.");
            break;
    }

    Theme::bodyFontOff(t);
    const Page pg = page();
    if (pg.one)  Theme::drawWin95Button(t, b.oneX, b.y, b.w, btnH(), pg.one, false);
    if (pg.left) Theme::drawWin95Button(t, b.leftX, b.y, b.w, btnH(), pg.left, false);
    if (pg.right) Theme::drawWin95Button(t, b.rightX, b.y, b.w, btnH(), pg.right, false);
}

InviteHit uiInviteHit(TFT_eSPI& t, int x, int y) {
    const Btns b = btns(t);
    const Page pg = page();
    const bool one = pg.one && in(x, y, b.oneX, b.y, b.w, btnH());
    const bool l   = pg.left && in(x, y, b.leftX, b.y, b.w, btnH());
    const bool r   = pg.right && in(x, y, b.rightX, b.y, b.w, btnH());
    switch (st()) {
        case InviteState::OFFERING: return one ? InviteHit::CANCEL : InviteHit::NONE;
        case InviteState::ASKED:    return l ? InviteHit::ACCEPT : (r ? InviteHit::DECLINE : InviteHit::NONE);
        case InviteState::CODE:     return l ? InviteHit::MATCH  : (r ? InviteHit::NOMATCH : InviteHit::NONE);
        case InviteState::SENDING:  return one ? InviteHit::BACK : InviteHit::NONE;
        case InviteState::WAITING:  return one ? InviteHit::CANCEL : InviteHit::NONE;
        case InviteState::JOINED:
        case InviteState::DONE:     return one ? InviteHit::BACK : InviteHit::NONE;
        case InviteState::FAILED:
            if (s_showPhrase || !inviter() || !Settings::phraseShown()) return one ? InviteHit::BACK : InviteHit::NONE;
            if (l) { s_showPhrase = true; return InviteHit::SHOW; }
            return r ? InviteHit::BACK : InviteHit::NONE;
        default:                    return one ? InviteHit::BACK : InviteHit::NONE;
    }
}
#endif // SQUACH_MESH
