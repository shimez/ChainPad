#pragma once
#include "LittleFS.h"
#include <stdexcept>
#include "probe_sdk.h"
using nvs_handle_t=int;
constexpr int ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_NVS_NOT_INITIALIZED=2;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
inline esp_err_t nvs_flash_init(){return ESP_OK;}
inline esp_err_t nvs_flash_deinit(){return ESP_OK;}
inline esp_err_t nvs_open(const char*,int,nvs_handle_t*){return ESP_ERR_NVS_NOT_FOUND;}
inline void nvs_close(nvs_handle_t){}
inline esp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t*){return ESP_ERR_NVS_NOT_FOUND;}
inline esp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t){throw std::runtime_error("Unexpected NVS write");}
inline esp_err_t nvs_commit(nvs_handle_t){throw std::runtime_error("Unexpected NVS commit");}
inline esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t){throw std::runtime_error("Unexpected erase");}
inline esp_err_t esp_littlefs_format(const char*){throw std::runtime_error("Unexpected format");}
