// The radio must notice an outfit/name/shade change while it is already
// advertising. Source uses this exact comparison on its live codec payload.
#include "mesh_appearance_policy.h"
#include "squachmesh.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    SquachMesh::Peer me = { 2, 3, 1, false, "" };
    uint8_t before[SquachMesh::LEN_MAX] = {};
    uint8_t after[SquachMesh::LEN_MAX] = {};
    const size_t original = SquachMesh::encode(me, before);
    assert(original == SquachMesh::LEN_INDEXED);
    assert(!MeshAppearancePolicy::changed(before, original, before, original));

    me.outfit = 4; // The owner changes outfit on either CYD while in range.
    size_t n = SquachMesh::encode(me, after);
    assert(n == original);
    assert(MeshAppearancePolicy::changed(after, n, before, original));
    memcpy(before, after, n);
    assert(!MeshAppearancePolicy::changed(after, n, before, n));

    me.shade = 2; // Shades should propagate without a restart too.
    n = SquachMesh::encode(me, after);
    assert(MeshAppearancePolicy::changed(after, n, before, original));
    memcpy(before, after, n);

    me.nick = 3;
    n = SquachMesh::encode(me, after);
    assert(MeshAppearancePolicy::changed(after, n, before, original));
    memcpy(before, after, n);

    me.custom = true;
    strcpy(me.name, "CYD 3.2");
    n = SquachMesh::encode(me, after);
    assert(n == SquachMesh::LEN_NAMED);
    assert(MeshAppearancePolicy::changed(after, n, before, original));
    memcpy(before, after, n);

    strcpy(me.name, "CYD 2.8");
    const size_t m = SquachMesh::encode(me, after);
    assert(m == n);
    assert(MeshAppearancePolicy::changed(after, m, before, n));
    memcpy(before, after, n);
    assert(!MeshAppearancePolicy::changed(after, m, before, n));

    me.custom = false;
    me.name[0] = '\0';
    n = SquachMesh::encode(me, after);
    assert(n == SquachMesh::LEN_INDEXED);
    assert(MeshAppearancePolicy::changed(after, n, before, m));
    assert(!MeshAppearancePolicy::changed(after, 0, before, 0));
    std::puts("PASS: outfit, shade and name updates change advertised payload");
}
