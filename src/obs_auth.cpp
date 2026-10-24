#include "capabilities.h"
#if CHAINPAD_HAS_OBS
#include "obs_auth.h"
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace chimera { namespace obs {
bool authentication(const char* password, const char* salt, const char* challenge, char* output) {
  if (!password || !salt || !challenge || !output || strlen(password) > 128 || strlen(salt) > 128 || strlen(challenge) > 128) return false;
  uint8_t digest[32]{}; char input[258]{}, secret[45]{}; size_t length = 0;
  snprintf(input, sizeof(input), "%s%s", password, salt);
  bool ok = mbedtls_sha256(reinterpret_cast<uint8_t*>(input), strlen(input), digest, 0) == 0 &&
    mbedtls_base64_encode(reinterpret_cast<uint8_t*>(secret), sizeof(secret), &length, digest, sizeof(digest)) == 0;
  if (ok) {
    snprintf(input, sizeof(input), "%s%s", secret, challenge);
    ok = mbedtls_sha256(reinterpret_cast<uint8_t*>(input), strlen(input), digest, 0) == 0 &&
      mbedtls_base64_encode(reinterpret_cast<uint8_t*>(output), 45, &length, digest, sizeof(digest)) == 0;
  }
  mbedtls_platform_zeroize(input, sizeof(input)); mbedtls_platform_zeroize(secret, sizeof(secret));
  mbedtls_platform_zeroize(digest, sizeof(digest)); return ok;
}
} }
#endif
