#pragma once
#include <cstddef>
#include <functional>
namespace stringFault {
inline int record=0,target=0,reserves=0,concats=0,failures=0,concatAt=0;
inline bool reserveFail=false,continuous=false;
inline size_t expected=0;
inline std::function<void()> beforeReserve;
inline void arm(int selected=0,bool reserve=false,int at=0,bool persistent=false){
  record=reserves=concats=failures=0;target=selected;reserveFail=reserve;concatAt=at;continuous=persistent;expected=0;beforeReserve={};
}
inline bool reserve(size_t n){if(beforeReserve)beforeReserve();++reserves;record=reserves;concats=0;expected=n;if(record==target&&reserveFail){++failures;return false;}return true;}
inline bool concat(){++concats;if(record==target&&concatAt>0&&(concats==concatAt||(continuous&&concats>=concatAt))){++failures;return false;}return true;}
}
