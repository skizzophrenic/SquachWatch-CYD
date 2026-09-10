// SquachWatch-CYD — the real cipher behind SquachMesh messages: mbedtls,
// AES-128-CCM with an 8-byte tag and PBKDF2-HMAC-SHA256, on the ESP32's
// hardware AES and SHA.
//
// Device only. The emulator links sim/meshcrypto_sim.cpp against this same
// header instead, and the host tests do not link a cipher at all -- see
// include/meshmsg.h for why, and for how the real one is still pinned.
#pragma once
#if SQUACH_MESH
#include "meshmsg.h"

namespace MeshCrypto {

const MeshMsg::Crypto& impl();

// Runs the golden frame from test/gen_meshmsg_vectors.py -- built by Python's
// `cryptography`, an implementation that shares no code with this one --
// through the real key stretching, sealing and opening, and demands the same
// bytes. False means this build's crypto disagrees with an independent
// implementation about what AES-CCM is, and it must not be trusted with a
// single message. Leaves no key set: call it before loading the real one.
bool selfTest();

} // namespace MeshCrypto
#endif
