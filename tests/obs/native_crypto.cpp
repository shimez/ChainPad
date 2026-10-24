#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <stddef.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
int mbedtls_sha256(const unsigned char* bytes, size_t size, unsigned char* out, int sha224) {
  if (sha224) return -1;
  BCRYPT_ALG_HANDLE algorithm;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return -1;
  auto result = BCryptHash(algorithm, nullptr, 0, const_cast<unsigned char*>(bytes), ULONG(size), out, 32);
  BCryptCloseAlgorithmProvider(algorithm, 0); return result ? -1 : 0;
}
int mbedtls_base64_encode(unsigned char* out, size_t capacity, size_t* written, const unsigned char* in, size_t size) {
  DWORD count = DWORD(capacity);
  if (!CryptBinaryToStringA(in, DWORD(size), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, reinterpret_cast<char*>(out), &count)) return -1;
  *written = count; return 0;
}
