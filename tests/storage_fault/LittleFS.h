#pragma once
#include "Arduino.h"
#include <map>
#include <memory>
#include <algorithm>
#include <functional>
#include "probe_state.h"
namespace storageFault {
inline std::map<std::string, std::shared_ptr<std::string>> files;
inline std::string failExistsPath;
inline int existsFailures=0, activeReadFailures=0, pendingWriteFailures=0, injected=0, renames=0;
inline bool failAfterRename=false, inExists=false;
inline std::string openFailurePath,readFailurePath;
inline void clearFaults(){failExistsPath.clear();openFailurePath.clear();readFailurePath.clear();existsFailures=activeReadFailures=pendingWriteFailures=injected=0;failAfterRename=false;}
}
class File {
  std::shared_ptr<std::string> data; size_t position=0; bool pending=false; std::string path;
public:
  File()=default;
  explicit File(std::shared_ptr<std::string> d,bool p=false,const char* name=""):data(d),pending(p),path(name){}
  explicit operator bool()const{return bool(data);}
  size_t size()const{return data?data->size():0;}
  int read(){if(storageFault::readFailurePath==path)return -1;return data&&position<data->size()?uint8_t((*data)[position++]):-1;}
  size_t readBytes(char* b,size_t n){size_t i=0;for(;i<n;++i){int c=read();if(c<0)break;b[i]=char(c);}return i;}
  size_t write(const uint8_t* b,size_t n){if(!data)return 0;if(pending&&storageFault::pendingWriteFailures){--storageFault::pendingWriteFailures;++storageFault::injected;return 0;}data->append(reinterpret_cast<const char*>(b),n);return n;}
  size_t write(uint8_t b){return write(&b,1);}
  void flush(){} void close(){data.reset();}
};
struct FaultFS {
  const char* mountpoint(){return probeFake::mountpoint.c_str();}
  bool testExists(const char* path){return storageFault::files.count(path);}
  bool begin(bool format,const char*,int,const char*){if(format&&probeFake::erased)probeFake::mounted=true;return probeFake::mounted;}
  void end(){}
  File open(const char* path,const char* mode){
    using namespace storageFault;
    if(*mode=='r'&&openFailurePath==path){++injected;return {};}
    if(*mode=='r' && inExists && failExistsPath==path && existsFailures){--existsFailures;++injected;return {};}
    if(*mode=='r' && !inExists && std::string(path)=="/config.records" && activeReadFailures){--activeReadFailures;++injected;return {};}
    if(*mode=='w')files[path]=std::make_shared<std::string>();
    auto it=files.find(path);return it==files.end()?File{}:File(it->second,std::string(path)=="/pending.records",path);
  }
  // Match installed SDK VFSImpl::exists: open failure is returned as false.
  bool exists(const char* path){storageFault::inExists=true;auto f=open(path,"r");storageFault::inExists=false;bool found=bool(f);f.close();return found;}
  bool rename(const char* from,const char* to){
    auto it=storageFault::files.find(from);if(it==storageFault::files.end())return false;
    storageFault::files[to]=it->second;storageFault::files.erase(it);++storageFault::renames;
    if(storageFault::failAfterRename && std::string(to)=="/config.records"){
      storageFault::activeReadFailures=1;storageFault::failAfterRename=false;
    }
    return true;
  }
};
inline FaultFS LittleFS;
