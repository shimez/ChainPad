#include "factory_reset.h"
#include "reset_fake.h"
#include <iostream>
using namespace chimera;
int main(){
  for(const char* config:{"valid settings","corrupt","oversized"}){
    fakeReset::clear();fakeReset::files["/config.records"]=config;fakeReset::files["/pending.records"]="staged";
    assert(factoryResetBoot()==ResetBoot::Normal);
    assert(requestFactoryReset()&&factoryResetBlocked());
    const auto writes=fakeReset::commits;assert(requestFactoryReset()&&fakeReset::commits==writes);
    assert(fakeReset::formats==0&&fakeReset::erases==0); // Acceptance does not erase.
    assert(factoryResetBoot()==ResetBoot::Restart);
    assert(!fakeReset::files.count("/config.records")&&!fakeReset::files.count("/pending.records")&&!fakeReset::bonds);
    assert(factoryResetBoot()==ResetBoot::Complete&&!factoryResetBlocked());
  }
  fakeReset::clear();fakeReset::mountFail=true;
  assert(factoryResetBoot()==ResetBoot::Normal);assert(requestFactoryReset());
  assert(factoryResetBoot()==ResetBoot::Restart); // Explicit reset formats even unmountable settings.
  fakeReset::clear();assert(factoryResetBoot()==ResetBoot::Normal);fakeReset::commitFail=true;
  assert(!requestFactoryReset()&&!factoryResetBlocked()&&fakeReset::formats==0);
  for(bool erase:{false,true}){
    fakeReset::clear();factoryResetBoot();assert(requestFactoryReset());
    if(erase)fakeReset::eraseFail=true;else fakeReset::formatFail=true;
    for(int i=0;i<3;++i)assert(factoryResetBoot()==ResetBoot::Failed&&factoryResetBlocked());
    const int operations=fakeReset::erases+fakeReset::formats;
    assert(factoryResetBoot()==ResetBoot::Failed);
    assert(fakeReset::erases+fakeReset::formats==operations);
    fakeReset::eraseFail=fakeReset::formatFail=false;assert(requestFactoryReset());
    assert(factoryResetBoot()==ResetBoot::Restart);
  }
  std::cout<<"PASS real reset adapter / valid-corrupt-oversized config / mount failure / duplicate request / request failure / erase failure / allowlisted partitions\n";
}
