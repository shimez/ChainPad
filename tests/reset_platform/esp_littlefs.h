#include "reset_fake.h"
inline esp_err_t esp_littlefs_format(const char* label){assert(std::string(label)=="settings");return LittleFS.format()?ESP_OK:9;}
