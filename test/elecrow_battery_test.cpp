#include "elecrow_battery.h"
#include <assert.h>
#include <stdio.h>

static unsigned reads;
static bool responds = true;
static uint16_t cell = 0xC800, soc = 0x5180, rate = 0;
static bool readRegister(uint8_t reg, uint16_t& value) {
    ++reads;
    if (!responds) return false;
    if (reg == 0x02) value = cell;
    else if (reg == 0x04) value = soc;
    else if (reg == 0x16) value = rate;
    else return false;
    return true;
}

int main() {
    ElecrowBattery::Gauge g;
    assert(!g.reading().valid);
    g.tick(0, readRegister);
    assert(g.reading().valid && g.reading().millivolts == 4000);
    assert(g.reading().percent == 81 && !g.reading().rising);
    const unsigned before = reads;
    g.tick(1999, readRegister);
    assert(reads == before);
    rate = 8;
    g.tick(2000, readRegister);
    assert(g.reading().rising);
    responds = false;
    g.tick(4000, readRegister);
    assert(!g.reading().valid); // Never retain a stale battery percentage.
    responds = true;
    g.tick(13999, readRegister);
    assert(!g.reading().valid);
    g.tick(14000, readRegister);
    assert(g.reading().valid);
    soc = 0xffff;
    g.tick(16000, readRegister);
    assert(!g.reading().valid);
    soc = 0x6400; cell = 0;
    g.tick(26000, readRegister);
    assert(!g.reading().valid);
    cell = 0xC800;
    g.tick(36000, readRegister);
    assert(g.reading().valid && g.reading().percent == 100);
    rate = (uint16_t)(int16_t)-10;
    g.tick(38000, readRegister);
    assert(!g.reading().rising);
    cell = 0xDC00; // 4.4 V: multiplication must not wrap at 32 bits.
    g.tick(40000, readRegister);
    assert(g.reading().valid && g.reading().millivolts == 4400);
    ElecrowBattery::Gauge wrap;
    wrap.tick(0xfffffff0u, readRegister);
    unsigned atWrap = reads;
    wrap.tick(0x10u, readRegister);
    assert(reads == atWrap);
    wrap.tick(0x800u, readRegister);
    assert(reads > atWrap);
    puts("elecrow_battery_test: PASS");
}
