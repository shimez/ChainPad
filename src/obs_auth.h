#pragma once
namespace chimera { namespace obs {
bool authentication(const char* password, const char* salt, const char* challenge, char* output);
} }
