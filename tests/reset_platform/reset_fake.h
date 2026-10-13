#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <algorithm>
#include <cassert>
using esp_err_t=int;using nvs_handle_t=int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_NOT_INITIALIZED=2;
constexpr int NVS_READONLY=0,NVS_READWRITE=1,ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_NVS=2;
namespace fakeReset {
inline std::map<std::string,std::string> files;
inline int attempt=-1,commits=0,erases=0,formats=0;
inline bool mountFail=false,formatFail=false,eraseFail=false,commitFail=false;
inline bool bonds=true;
inline void clear(){files.clear();attempt=-1;commits=erases=formats=0;mountFail=formatFail=eraseFail=commitFail=false;bonds=true;}
}
struct File {
  std::string path;bool valid=false;
  operator bool()const{return valid;}
  size_t size(){return fakeReset::files[path].size();}
  size_t readBytes(char* b,size_t n){auto& s=fakeReset::files[path];n=std::min(n,s.size());memcpy(b,s.data(),n);return n;}
  size_t write(const uint8_t* b,size_t n){fakeReset::files[path].assign(reinterpret_cast<const char*>(b),n);return n;}
  void flush(){}void close(){}
};
struct FakeFS {
  bool begin(bool format,const char*,int,const char* label){assert(!format&&std::string(label)=="settings");return !fakeReset::mountFail;}
  void end(){}
  bool format(){++fakeReset::formats;if(fakeReset::formatFail)return false;fakeReset::files.clear();fakeReset::mountFail=false;return true;}
  bool exists(const char* p){return fakeReset::files.count(p);}
  File open(const char* p,const char* mode){if(*mode=='w')fakeReset::files[p]="";return {p,bool(fakeReset::files.count(p))};}
  bool rename(const char* from,const char* to){if(!exists(from))return false;fakeReset::files[to]=fakeReset::files[from];fakeReset::files.erase(from);return true;}
};
inline FakeFS LittleFS;
inline esp_err_t nvs_flash_init(){return ESP_OK;}
inline esp_err_t nvs_flash_deinit(){return ESP_OK;}
inline esp_err_t nvs_open(const char* ns,int mode,nvs_handle_t* h){assert(std::string(ns)=="chainpad_reset");*h=1;return mode==NVS_READONLY&&fakeReset::attempt==-1?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
inline void nvs_close(nvs_handle_t){}
inline esp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t* v){if(fakeReset::attempt<0)return ESP_ERR_NVS_NOT_FOUND;*v=fakeReset::attempt;return ESP_OK;}
inline esp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t v){if(fakeReset::commitFail)return 9;fakeReset::attempt=v;return ESP_OK;}
inline esp_err_t nvs_commit(nvs_handle_t){++fakeReset::commits;return ESP_OK;}
struct esp_partition_t {size_t size;};
inline esp_partition_t nvsPartition{0x5000};
inline const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* name){assert(type==ESP_PARTITION_TYPE_DATA&&subtype==ESP_PARTITION_SUBTYPE_DATA_NVS&&std::string(name)=="nvs");return &nvsPartition;}
inline esp_err_t esp_partition_erase_range(const esp_partition_t* p,size_t offset,size_t length){assert(p==&nvsPartition&&offset==0&&length==p->size);++fakeReset::erases;if(fakeReset::eraseFail)return 9;fakeReset::attempt=-1;fakeReset::bonds=false;return ESP_OK;}
inline esp_err_t esp_partition_read(const esp_partition_t* p,size_t offset,void* b,size_t n){assert(p==&nvsPartition&&offset+n<=p->size);memset(b,0xff,n);return ESP_OK;}
