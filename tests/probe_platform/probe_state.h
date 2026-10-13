#pragma once
#include <cerrno>
#include <map>
#include <set>
#include <string>
namespace probeFake {
inline bool mounted=true, registered=true, erased=false, partitionMissing=false;
inline std::string mountpoint="/littlefs";
inline int rootError=0, successErrno=0, partitionReadFailure=-1, partitionReads=0;
inline std::map<std::string,int> statErrors;
inline std::set<std::string> directories;
inline void clear(){mounted=registered=true;erased=partitionMissing=false;mountpoint="/littlefs";rootError=successErrno=0;partitionReadFailure=-1;partitionReads=0;statErrors.clear();directories.clear();}
}
