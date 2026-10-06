#include "mini_log.h"
#include <cassert>
#include <cstring>
struct Row { int rssi; };
struct Engine {
    Row rows[5] = {{-90}, {-50}, {-73}, {-50}, {-99}};
    uint8_t logCount() const { return 5; }
    const Row* logAt(unsigned i) const { return &rows[i]; }
};
int main() {
    Engine e; uint8_t order[5];
    assert(MiniLog::strongestFirst(e, order, 5) == 5);
    uint8_t expected[] = {1,3,2,0,4};
    assert(!memcmp(order, expected, 5));
    assert(e.rows[0].rssi == -90);
    assert(MiniLog::strongestFirst(e, order, 2) == 2 && order[0] == 1 && order[1] == 0);
    assert(MiniLog::strongestFirst(e, order, 0) == 0);
    assert(MiniLog::bars(-49) == 4 && MiniLog::bars(-50) == 4);
    assert(MiniLog::bars(-51) == 3 && MiniLog::bars(-65) == 3);
    assert(MiniLog::bars(-66) == 2 && MiniLog::bars(-80) == 2);
    assert(MiniLog::bars(-81) == 1 && MiniLog::bars(-90) == 1);
    assert(MiniLog::bars(-91) == 0);
    char b[8];
    MiniLog::age(1000,1000,b,sizeof b); assert(!strcmp(b,"0:00"));
    MiniLog::age(901000,1000,b,sizeof b); assert(!strcmp(b,"15:00"));
    MiniLog::age(1000,1001,b,sizeof b); assert(!strcmp(b,"0:00"));
    MiniLog::age(500,0xfffffe0c,b,sizeof b); assert(!strcmp(b,"0:01"));
    MiniLog::age(60000000,0,b,sizeof b); assert(!strcmp(b,"16h40m"));
    MiniLog::age(360000000,0,b,sizeof b); assert(!strcmp(b,"4d04h"));
    puts("mini_log_test PASS");
}
