// Protect the main-screen visitor from stale appearance snapshots.
// Squad already reads live peer data; CLEAR must mirror it for the SAME
// visitor without treating a changed outfit as a replacement identity.
#include "mesh_visit_appearance.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    SquachMesh::Peer hosting = { 1, 2, 0, false, "" };
    SquachMesh::Peer live = hosting;
    const uint32_t alice = 0x1234u, bob = 0x5678u;

    // Unchanged look: no needless updates or animation state churn.
    assert(!MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));

    // Live outfit and shades must propagate with the visitor still present.
    live.outfit = 5;
    live.shade = 3;
    assert(MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));
    assert(hosting.outfit == 5 && hosting.shade == 3);
    assert(!MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));

    // An indexed nickname or custom name can change independently.
    live.nick = 8;
    assert(MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));
    live.custom = true;
    strcpy(live.name, "NEIGHBOR");
    assert(MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));
    assert(hosting.custom && strcmp(hosting.name, "NEIGHBOR") == 0);
    live.custom = false;
    live.name[0] = '\0';
    assert(MeshVisitAppearance::refresh(hosting, alice, alice, &live, false));
    assert(!hosting.custom && hosting.name[0] == '\0');

    const SquachMesh::Peer snapshot = hosting;
    // A different Squachy may arrive, but must not overwrite this guest's
    // face until the old one has completed the goodbye walk.
    live.outfit = 10;
    assert(!MeshVisitAppearance::refresh(hosting, alice, bob, &live, false));
    assert(hosting.outfit == snapshot.outfit);
    assert(!MeshVisitAppearance::refresh(hosting, alice, alice, &live, true));
    assert(hosting.outfit == snapshot.outfit);
    assert(!MeshVisitAppearance::refresh(hosting, alice, 0, nullptr, false));
    assert(!MeshVisitAppearance::refresh(hosting, 0, alice, &live, false));
    assert(hosting.outfit == snapshot.outfit);

    std::puts("PASS: live same-peer outfit and nickname refresh, identity and exit preserved");
}
