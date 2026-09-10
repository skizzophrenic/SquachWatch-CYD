// SquachWatch-CYD — mbedtls behind SquachMesh messages. See include/meshcrypto.h.
#include "meshcrypto.h"

#if SQUACH_MESH
#include "meshmsg_vectors.h"
#include "mbedtls/ccm.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include <string.h>

namespace {

// One cipher context for the life of the program, keyed when a key is loaded
// and reused for every message. mbedtls_ccm_setkey allocates, so setting up
// per message would be allocation churn on the one heap this project has had
// to fight; per key, it happens a handful of times a year.
mbedtls_ccm_context s_ccm;
bool s_ccmInit = false;
bool s_keyed   = false;

bool pbkdf2Derive(const char* phrase, size_t plen, const uint8_t* salt, size_t slen,
                  uint32_t iters, uint8_t key[MeshMsg::KEY_LEN]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    // The HMAC context is the one allocation in this whole path. Transient,
    // a couple of hundred bytes, and checked -- a failed allocation here is a
    // failed derive, never a key made of whatever was in the buffer.
    bool ok = mbedtls_md_setup(&md, info, 1) == 0 &&
              mbedtls_pkcs5_pbkdf2_hmac(&md, (const unsigned char*)phrase, plen,
                                        salt, slen, iters,
                                        (uint32_t)MeshMsg::KEY_LEN, key) == 0;
    mbedtls_md_free(&md);
    return ok;
}

bool ccmSetKey(const uint8_t key[MeshMsg::KEY_LEN]) {
    if (!s_ccmInit) { mbedtls_ccm_init(&s_ccm); s_ccmInit = true; }
    s_keyed = mbedtls_ccm_setkey(&s_ccm, MBEDTLS_CIPHER_ID_AES, key,
                                 (unsigned int)(MeshMsg::KEY_LEN * 8)) == 0;
    return s_keyed;
}

bool ccmSeal(const uint8_t nonce[MeshMsg::NONCE_LEN], const uint8_t* aad, size_t aadLen,
             const uint8_t* pt, size_t ptLen, uint8_t* ct, uint8_t tag[MeshMsg::TAG_LEN]) {
    return s_keyed &&
           mbedtls_ccm_encrypt_and_tag(&s_ccm, ptLen, nonce, MeshMsg::NONCE_LEN,
                                       aad, aadLen, pt, ct, tag, MeshMsg::TAG_LEN) == 0;
}

bool ccmOpen(const uint8_t nonce[MeshMsg::NONCE_LEN], const uint8_t* aad, size_t aadLen,
             const uint8_t* ct, size_t ctLen, const uint8_t tag[MeshMsg::TAG_LEN], uint8_t* pt) {
    return s_keyed &&
           mbedtls_ccm_auth_decrypt(&s_ccm, ctLen, nonce, MeshMsg::NONCE_LEN,
                                    aad, aadLen, ct, pt, tag, MeshMsg::TAG_LEN) == 0;
}

const MeshMsg::Crypto IMPL = { pbkdf2Derive, ccmSetKey, ccmSeal, ccmOpen };

} // namespace

const MeshMsg::Crypto& MeshCrypto::impl() { return IMPL; }

bool MeshCrypto::selfTest() {
    namespace V = MeshMsgVec;
    bool ok = false;
    do {
        // 1. Key stretching agrees.
        uint8_t key[MeshMsg::KEY_LEN];
        if (!pbkdf2Derive(V::PHRASE, strlen(V::PHRASE),
                          (const uint8_t*)MeshMsg::SALT, strlen(MeshMsg::SALT),
                          V::ITERS, key)) break;
        if (memcmp(key, V::KEY, sizeof key) != 0) break;
        if (!ccmSetKey(key)) break;

        // 2. The whole frame agrees -- header, nonce, associated data,
        //    ciphertext and tag, all at once.
        uint8_t f[MeshMsg::CANNED_FRAME_LEN];
        if (MeshMsg::sealCanned(IMPL, V::MAC, V::COUNTER, V::CANNED, f, sizeof f) != sizeof f) break;
        if (memcmp(f, V::FRAME, sizeof f) != 0) break;

        // 3. The golden frame opens, to the golden line.
        uint32_t ctr = 0;
        uint8_t line = 0xFF;
        if (MeshMsg::openCanned(IMPL, V::MAC, V::FRAME, sizeof V::FRAME, ctr, line)
                != MeshMsg::Open::OK) break;
        if (ctr != V::COUNTER || line != V::CANNED) break;

        // 4. And a tampered one does not. A cipher that accepts everything
        //    passes the first three.
        f[MeshMsg::HDR_LEN] ^= 0x01;
        if (MeshMsg::openCanned(IMPL, V::MAC, f, sizeof f, ctr, line)
                != MeshMsg::Open::BAD_TAG) break;
        ok = true;
    } while (false);
    s_keyed = false;            // nothing left keyed with the test key
    return ok;
}

#endif // SQUACH_MESH
