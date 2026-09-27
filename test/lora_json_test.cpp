// The JSON scanner -- src/lora_json.cpp -- on its own.
//
// It has a test of its own because it is not a convenience: it IS the allow-list
// that both online sources rely on (include/lora_enrich.h rule 4), and the bugs
// it can have are the quiet kind. An escape mishandled by one byte makes the
// rest of a value read as the document's other fields, which is how a street
// address ends up in a field a reviewer checked was a city. So the cases below
// are mostly the awkward ones.
#include "lora_json.h"
#include "test_util.h"
#include <cstring>

using namespace Lora;

namespace {
// A body shaped like the ones this firmware really reads: a wrapper, a nested
// object with keys that are duplicated in its siblings, and an array of objects.
const char* DOC =
    "{\"success\":true,\"source\":\"outer\","
    "\"callsign\":{\"city\":\"Arnsberg\",\"source\":\"inner\",\"grid_square\":\"JO41ak\","
    "\"note\":\"a \\\"quoted\\\" word, a \\\\ backslash and a \\u00fc\",\"empty\":\"\",\"nul\":null,"
    "\"num\":42,\"sub\":{\"city\":\"Nested\"}},"
    "\"other\":{\"city\":\"Sibling\"},"
    "\"adverts\":[{\"name\":\"one\"},{\"name\":\"two\",\"inner\":{\"name\":\"not a row\"}},{\"name\":\"three\"}]}";

const char* end(const char* s) { return s + strlen(s); }
}  // namespace

int main() {
    const char* b = DOC;
    const char* e = end(DOC);
    char v[64];

    suite("reading a string");
    {
        ck("a plain value", Json::str(b, e, "success", v, sizeof v) == false);  // true is not a string
        ck("a string value", Json::str(b, e, "source", v, sizeof v) && strcmp(v, "outer") == 0);
        ck("a number is not a string", !Json::str(b, e, "num", v, sizeof v));
        ck("null is not a string", !Json::str(b, e, "nul", v, sizeof v));
        ck("an empty string reads as absent", !Json::str(b, e, "empty", v, sizeof v));
        ck("a missing key", !Json::str(b, e, "nosuchkey", v, sizeof v) && v[0] == '\0');
        // The bug this function exists to not have: an escaped quote inside a
        // value must not end the value.
        ck("an escaped quote does not end the value",
           Json::str(b, e, "note", v, sizeof v) &&
           strcmp(v, "a \"quoted\" word, a \\ backslash and a ?") == 0);
        // The fonts are ASCII; a \u escape is four hex digits to step over and
        // one question mark to show, not five stray characters.
        ck("a \\u escape is one character wide", strchr(v, '?') != nullptr && strstr(v, "00fc") == nullptr);
        // Truncation must still terminate.
        char small[6];
        ck("a short buffer truncates and terminates",
           Json::str(b, e, "source", small, sizeof small) && strcmp(small, "outer") == 0);
        char tiny[4];
        Json::str(b, e, "source", tiny, sizeof tiny);
        ck("and a tiny one", strcmp(tiny, "out") == 0);
        ck("a zero cap is refused", !Json::str(b, e, "source", v, 0));
    }

    suite("reading inside one object");
    {
        const char* ob = nullptr;
        const char* oe = nullptr;
        ck("the named object is found", Json::obj(b, e, "callsign", ob, oe));
        ck("it starts at its brace and ends past the matching one", *ob == '{' && *(oe - 1) == '}');
        // The whole reason obj() exists: `source` and `city` appear in the
        // wrapper and in a sibling, and a search of the document would find the
        // wrong one.
        ck("a key is read from THIS object", Json::str(ob, oe, "source", v, sizeof v) &&
                                            strcmp(v, "inner") == 0);
        ck("and the sibling's value is not visible", Json::str(ob, oe, "city", v, sizeof v) &&
                                                    strcmp(v, "Arnsberg") == 0);
        const char* sb = nullptr;
        const char* se = nullptr;
        ck("a nested object is found within it", Json::obj(ob, oe, "sub", sb, se) &&
                                                Json::str(sb, se, "city", v, sizeof v) &&
                                                strcmp(v, "Nested") == 0);
        ck("an array is not an object", !Json::obj(b, e, "adverts", ob, oe));
        ck("a missing object", !Json::obj(b, e, "nosuchobject", ob, oe));
        // Brace counting has to be string-aware or a brace inside a name would
        // close the object early.
        const char* braced = "{\"o\":{\"name\":\"a } brace\",\"city\":\"Real\"}}";
        const char* bb = nullptr;
        const char* be = nullptr;
        ck("a brace inside a string does not close the object",
           Json::obj(braced, end(braced), "o", bb, be) &&
           Json::str(bb, be, "city", v, sizeof v) && strcmp(v, "Real") == 0);
    }

    suite("walking an array of objects");
    {
        const char* ab = nullptr;
        const char* ae = nullptr;
        ck("the array is found by name", Json::arr(b, e, "adverts", ab, ae));
        ck("it is bracketed", *ab == '[' && *(ae - 1) == ']');
        const char* p = ab;
        const char* ob = nullptr;
        const char* oe = nullptr;
        int n = 0;
        char names[8][16];
        while (n < 8 && Json::nextObj(p, ae, ob, oe)) {
            Json::str(ob, oe, "name", names[n], sizeof names[n]);
            n++;
        }
        // Three rows, not four: the nested object inside the second row is part
        // of that row and must not be handed back as a row of its own.
        ck("three rows, not four", n == 3);
        ck("in order", strcmp(names[0], "one") == 0 && strcmp(names[1], "two") == 0 &&
                       strcmp(names[2], "three") == 0);
        ck("an empty array walks to nothing",
           Json::arr("{\"a\":[]}", end("{\"a\":[]}"), "a", ab, ae) && !Json::nextObj(ab, ae, ob, oe));
        // A truncated array -- what a fixed read buffer does to a long feed --
        // has no closing bracket, so arr() fails and the caller walks the rest.
        const char* cut = "{\"adverts\":[{\"name\":\"one\"},{\"name\":\"tw";
        ck("a truncated array is not found as an array", !Json::arr(cut, end(cut), "adverts", ab, ae));
        p = cut;
        n = 0;
        while (Json::nextObj(p, end(cut), ob, oe)) n++;
        ck("but the complete rows before the cut still walk", n == 1);
    }

    suite("the literal search");
    {
        ck("a present literal", Json::has(b, e, "\"grid_square\""));
        ck("an absent one", !Json::has(b, e, "\"NOT_FOUND\""));
        ck("and the exact bytes, not a prefix", !Json::has(b, e, "\"grid_squares\""));
        ck("an empty range finds nothing", !Json::has(b, b, "\"success\""));
    }

    return report();
}
