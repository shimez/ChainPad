#pragma once
#include "probe_state.h"
#include <cstdint>
#include <cstring>
#include <cassert>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_NVS=2,ESP_PARTITION_SUBTYPE_DATA_SPIFFS=3;
struct esp_partition_t {size_t size;};
inline esp_partition_t nvsPartition{0x5000},settingsPartition{513};
inline bool esp_littlefs_mounted(const char* label){assert(std::string(label)=="settings");return probeFake::mounted;}
inline const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* name){
  assert(type==ESP_PARTITION_TYPE_DATA);
  if(std::string(name)=="settings"){assert(subtype==ESP_PARTITION_SUBTYPE_DATA_SPIFFS);return probeFake::partitionMissing?nullptr:&settingsPartition;}
  assert(std::string(name)=="nvs"&&subtype==ESP_PARTITION_SUBTYPE_DATA_NVS);return &nvsPartition;
}
inline esp_err_t esp_partition_read(const esp_partition_t* p,size_t offset,void* b,size_t n){
  assert(offset+n<=p->size);
  if(p==&settingsPartition){if(probeFake::partitionReads++==probeFake::partitionReadFailure)return 9;memset(b,probeFake::erased?0xff:0,n);}
  else {assert(p==&nvsPartition);memset(b,0xff,n);}
  return ESP_OK;
}
