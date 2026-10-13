#pragma once
#include "LittleFS.h"
#include <stdexcept>
#include "probe_sdk.h"
using nvs_handle_t=int;
constexpr int ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_NVS_NOT_INITIALIZED=2;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
namespace resetFault {inline int intent=-1,commits=0;}
inline esp_err_t nvs_flash_init(){return ESP_OK;}
inline esp_err_t nvs_flash_deinit(){return ESP_OK;}
inline esp_err_t nvs_open(const char*,int mode,nvs_handle_t* h){*h=1;return mode==NVS_READONLY&&resetFault::intent<0?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
inline void nvs_close(nvs_handle_t){}
inline esp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t* v){if(resetFault::intent<0)return ESP_ERR_NVS_NOT_FOUND;*v=uint8_t(resetFault::intent);return ESP_OK;}
inline esp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t v){resetFault::intent=v;return ESP_OK;}
inline esp_err_t nvs_commit(nvs_handle_t){++resetFault::commits;return ESP_OK;}
inline esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t){throw std::runtime_error("Unexpected erase");}
inline esp_err_t esp_littlefs_format(const char*){throw std::runtime_error("Unexpected format");}
