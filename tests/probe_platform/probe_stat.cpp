#include "probe_state.h"
#include <LittleFS.h>
#include <sys/stat.h>
int stat(const char* path,struct stat* info){
  using namespace probeFake;
  std::string p=path;
  if(!registered){errno=ENOENT;return -1;}
  if(p=="/littlefs"){if(rootError){errno=rootError;return -1;}info->st_mode=S_IFDIR;return 0;}
  if(p.rfind("/littlefs/",0)!=0){errno=ENOENT;return -1;}
  p=p.substr(9);
  auto it=statErrors.find(p);if(it!=statErrors.end()){errno=it->second;return -1;}
  if(directories.count(p)){info->st_mode=S_IFDIR;return 0;}
  if(!LittleFS.testExists(p.c_str())){errno=ENOENT;return -1;}
  info->st_mode=S_IFREG;errno=successErrno;return 0;
}
