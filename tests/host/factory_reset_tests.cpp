#include "factory_reset_flow.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace chimera;
struct PowerLoss {};
struct Store {
  int n=-1, j=-1, cut=0, calls=0;
  bool done=false, settings=true, bonds=true;
  std::string fail;
  std::vector<std::string> operations;
  bool op(const char* name) { operations.push_back(name);return fail!=name; }
  void after() { if(++calls==cut)throw PowerLoss{}; }
  int intent(){return n;} int journal(){return j;} bool completed(){return done;}
  bool saveIntent(int v){if(!op("intent"))return false;n=v;after();return true;}
  bool formatSettings(){if(!op("format"))return false;settings=false;j=-1;done=false;after();return true;}
  bool saveJournal(int v){if(!op("journal"))return false;j=v;after();return true;}
  bool eraseNvs(){if(!op("erase"))return false;n=-1;bonds=false;after();return true;}
  bool finish(){if(!op("finish"))return false;j=-1;done=true;after();return true;}
};
int main(){
  Store s;assert(runFactoryReset(s)==ResetBoot::Normal && s.operations.empty());
  s.n=0;assert(runFactoryReset(s)==ResetBoot::Restart);
  assert((s.operations==std::vector<std::string>{"intent","format","journal","erase","finish"}));
  assert(!s.settings&&!s.bonds&&s.done);
  s.operations.clear();assert(runFactoryReset(s)==ResetBoot::Complete&&s.operations.empty());
  // Actual Config records (valid/corrupt/overcapacity) are deliberately not read
  // by this flow. A failed mount/format cannot turn into a successful reset.
  for(const char* step:{"intent","format","journal","erase","finish"}){
    Store f;f.n=0;f.fail=step;assert(runFactoryReset(f)==ResetBoot::Failed);
    if(f.fail=="intent"||f.fail=="format")assert(f.bonds);
    f.fail="";assert(runFactoryReset(f)==ResetBoot::Restart);
  }
  for(int cut=1;cut<=5;++cut){
    Store f;f.n=0;f.cut=cut;
    try{runFactoryReset(f);assert(false);}catch(PowerLoss&){}
    f.cut=0;const auto outcome=runFactoryReset(f);
    assert(outcome==ResetBoot::Restart||outcome==ResetBoot::Complete);
    assert(!f.settings&&!f.bonds&&f.done);
  }
  // NVS partly erased/unreadable: filesystem journal still authorizes recovery.
  Store partial;partial.n=-2;partial.j=1;partial.settings=false;
  assert(runFactoryReset(partial)==ResetBoot::Restart);
  for(bool phase:{false,true}){
    Store f;f.n=0;f.fail=phase?"erase":"format";
    for(int i=0;i<3;++i)assert(runFactoryReset(f)==ResetBoot::Failed);
    const auto count=f.operations.size();
    assert(runFactoryReset(f)==ResetBoot::Failed && f.operations.size()==count);
    // New, explicit user confirmation grants another bounded attempt sequence.
    f.n=0;f.fail="";assert(runFactoryReset(f)==ResetBoot::Restart);
  }
  Store invalid;invalid.j=-2;assert(runFactoryReset(invalid)==ResetBoot::Failed&&invalid.operations.empty());
  invalid.n=0;assert(runFactoryReset(invalid)==ResetBoot::Restart);
  Store unreadable;unreadable.n=-2;assert(runFactoryReset(unreadable)==ResetBoot::Failed&&unreadable.operations.empty());
  std::cout<<"PASS Factory Reset ordering / recovery / failure stops / power cuts / partial NVS / attempt limit / explicit retry\n";
}
