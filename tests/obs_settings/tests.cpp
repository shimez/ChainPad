#include "obs_settings_store.h"
#include "storage_probe.h"
#include <cassert>
#include <map>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "FAIL line " << __LINE__ << '\n'; std::exit(1); } } while (false)
namespace io {
using namespace chimera;
constexpr const char* Active = "/littlefs/obs-settings.json";
constexpr const char* Temp = "/littlefs/obs-settings.tmp";
std::map<std::string,std::string> files;
struct Handle { std::string path; size_t pos=0; bool write=false; };
std::map<int,Handle> handles;
int next=3, writes=0, renames=0;
bool mounted=true, probeError=false, failOpenRead=false, failOpenWrite=false,
     shortWrite=false, noSpace=false, failSync=false, failClose=false, failRead=false,
     earlyEof=false, failStat=false, failRename=false, damageTemp=false, alterTemp=false,
     failWriteClose=false, failTempOpen=false, failTempRead=false;
int open(const char* path, int flags, int = 0) {
  bool write=(flags&O_WRONLY)!=0;
  assert(std::string(path)==Active || std::string(path)==Temp);
  if(!mounted||(write?failOpenWrite:failOpenRead))return -1;
  if(!write&&std::string(path)==Temp&&failTempOpen)return -1;
  if(write){assert(std::string(path)==Temp);files[path]="";}
  else if(!files.count(path))return -1;
  int fd=next++;handles[fd]={path,0,write};return fd;
}
int fstat(int fd, struct stat* s) {
  if(failStat)return -1;
  s->st_size=static_cast<decltype(s->st_size)>(files[handles.at(fd).path].size());return 0;
}
int read(int fd, void* out, size_t n) {
  if(failRead)return -1;
  if(earlyEof)return 0;
  auto& h=handles.at(fd);auto& bytes=files[h.path];
  if(h.path==Temp&&failTempRead)return -1;
  n=std::min(n,bytes.size()-h.pos); memcpy(out,bytes.data()+h.pos,n);h.pos+=n;return int(n);
}
int write(int fd,const void* data,size_t n) {
  ++writes; if(noSpace)return -1;
  if(shortWrite&&n)--n;
  files[handles.at(fd).path].append(static_cast<const char*>(data),n);return int(n);
}
int fsync(int) {return failSync?-1:0;}
int close(int fd) {
  auto h=handles.at(fd);handles.erase(fd);
  if(h.write&&damageTemp)files[h.path]="{";
  if(h.write&&alterTemp)files[h.path]="{\"storageVersion\":1,\"enabled\":false,\"ip\":\"\",\"port\":123,\"password\":\"\"}";
  return (failClose||(h.write&&failWriteClose))?-1:0;
}
int rename(const char* from,const char* to) {
  ++renames;assert(std::string(from)==Temp&&std::string(to)==Active);
  if(failRename)return -1;
  files[to]=files.at(from);files.erase(from);return 0;
}
void reset(){
  assert(handles.empty());files.clear();next=3;writes=renames=0;mounted=true;
  probeError=failOpenRead=failOpenWrite=shortWrite=noSpace=failSync=failClose=failRead=earlyEof=failStat=failRename=damageTemp=alterTemp=false;
  failWriteClose=failTempOpen=failTempRead=false;
  files["/littlefs/config.records"]="existing-config";files["/littlefs/pending.records"]="existing-transaction";
}
void intact(){
  assert(files.at("/littlefs/config.records")=="existing-config");
  assert(files.at("/littlefs/pending.records")=="existing-transaction");assert(handles.empty());
}
}
namespace chimera {
StoragePresence probeSettingsFile(const char* path) {
  assert(!strcmp(path,"/obs-settings.json"));
  if(!io::mounted||io::probeError)return StoragePresence::Error;
  return io::files.count(io::Active)?StoragePresence::Present:StoragePresence::Absent;
}
}
// Compile the actual production store with only the VFS syscall boundary mocked.
#define open io::open
#define fstat io::fstat
#define read io::read
#define write io::write
#define fsync io::fsync
#define close io::close
#define rename io::rename
#include "../../src/obs_settings_store.cpp"
#undef open
#undef fstat
#undef read
#undef write
#undef fsync
#undef close
#undef rename
using namespace chimera;
using namespace chimera::obssettings;
const std::string update="{\"enabled\":true,\"ip\":\"192.0.2.1\",\"port\":4455,\"password\":\"fixture-private\"}";
const std::string omit="{\"enabled\":false,\"ip\":\"192.0.2.2\",\"port\":1234}";
Error save(Store& s,const std::string& j,bool replace=false){return s.save(j.data(),j.size(),replace);}
Error decode(const std::string& j,ObsSettings& out){return decodeUpdate(j.data(),j.size(),nullptr,out);}
void invalid(const std::string& j){ObsSettings out;out.port=77;assert(decode(j,out)!=Error::None);assert(out.port==77);}
int main(){
  Error e;ObsSettings initial;
  assert(!initial.enabled&&!initial.ip[0]&&initial.port==4455&&!initial.password[0]);
  io::reset();Store s;assert(!s.settings());assert(s.load(e)==State::Missing&&e==Error::None);
  assert(s.settings()&&equal(*s.settings(),initial)&&io::writes==0);
  assert(save(s,omit)==Error::None&&!s.settings()->password[0]);
  assert(save(s,update)==Error::None&&s.state()==State::Ready);
  Store reboot;assert(reboot.load(e)==State::Ready&&equal(*s.settings(),*reboot.settings()));
  assert(save(s,omit)==Error::None&&!s.settings()->enabled&&!strcmp(s.settings()->password,"fixture-private"));
  assert(!strcmp(s.settings()->ip,"192.0.2.2"));
  auto clear=omit;clear.insert(clear.size()-1,",\"password\":\"\"");assert(save(s,clear)==Error::None&&!s.settings()->password[0]);
  auto change=omit;change.insert(change.size()-1,",\"password\":\"  unchanged whitespace  \"");
  assert(save(s,change)==Error::None&&!strcmp(s.settings()->password,"  unchanged whitespace  "));
  char wire[1025];auto n=encodePublic(*s.settings(),wire,sizeof(wire));assert(n);
  JsonDocument pub;assert(!deserializeJson(pub,wire));assert(pub["password"].isUnbound()&&pub["passwordConfigured"]==true);
  assert(std::string(wire).find("unchanged whitespace")==std::string::npos);
  char tiny[2];assert(!encodePublic(*s.settings(),tiny,sizeof(tiny))&&tiny[0]==0);
  auto snapshot=*s.settings();auto disk=io::files.at(io::Active);int writes=io::writes;
  assert(save(s,"{}")!=Error::None&&equal(*s.settings(),snapshot));
  assert(io::files.at(io::Active)==disk&&io::writes==writes);
  auto escaped=omit;escaped.insert(escaped.size()-1,",\"password\":\"quote\\\" slash\\\\ \\u00e9\"");
  assert(save(s,escaped)==Error::None);
  assert(reboot.load(e)==State::Ready&&equal(*s.settings(),*reboot.settings()));
  assert(!strcmp(s.settings()->password,"quote\" slash\\ \xc3\xa9"));
  io::intact();std::cout<<"PASS normal/missing/roundtrip/password update/public privacy\n";

  for(const char* ip:{"localhost","http://192.0.2.1","::1","1.2.3","256.1.2.3","1..2.3","1.2.3.4 ","+1.2.3.4",""}){
    invalid("{\"enabled\":true,\"ip\":\""+std::string(ip)+"\",\"port\":4455,\"password\":\"\"}");
  }
  for(const char* port:{"0","65536","-1","1.5","4455.0","true","null","\"4455\""}){
    invalid("{\"enabled\":false,\"ip\":\"\",\"port\":"+std::string(port)+",\"password\":\"\"}");
  }
  for(const char* value:{"null","12","true","{}","[]","\"\\u0000\"","\"\\n\"","\"\\t\"","\"\\u007f\""})
    invalid("{\"enabled\":false,\"ip\":\"\",\"port\":4455,\"password\":"+std::string(value)+"}");
  invalid("{\"enabled\":1,\"ip\":\"\",\"port\":4455,\"password\":\"\"}");
  invalid("{\"enabled\":false,\"ip\":123,\"port\":4455,\"password\":\"\"}");
  invalid(update+"false");invalid(update+std::string(1,'\0'));invalid("{");invalid("[]");invalid(std::string(1025,' '));
  invalid("{\"enabled\":false,\"ip\":\"\",\"port\":4455,\"password\":\"\",\"extra\":{\"nested\":{}}}");
  for(auto password:{std::string(129,'a'),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xc2",1)})
    invalid("{\"enabled\":false,\"ip\":\"\",\"port\":4455,\"password\":\""+password+"\"}");
  for(auto password:{std::string(128,'a'),std::string(126,'a')+"\xc3\xa9"}){
    auto j="{\"enabled\":false,\"ip\":\"\",\"port\":65535,\"password\":\""+password+"\"}";
    ObsSettings out;assert(decode(j,out)==Error::None&&strlen(out.password)==128);
  }
  std::cout<<"PASS strict IPv4/types/UTF8/NUL/control/128-byte/depth/size/trailing input\n";

  for(auto malformed:{std::string("{"),std::string(1025,'x'),std::string("{\"storageVersion\":1,\"enabled\":false,\"ip\":\"\",\"port\":4455}"),std::string("{\"storageVersion\":2}")}){
    io::reset();io::files[io::Active]=malformed;Store broken;
    auto state=broken.load(e);assert(state==(malformed=="{\"storageVersion\":2}"?State::UnsupportedVersion:State::Corrupt));
    assert(!broken.settings()&&io::writes==0);
    assert(save(broken,update)==Error::ReplaceRequired&&io::files.at(io::Active)==malformed);
    assert(save(broken,omit,true)==Error::PasswordRequired&&io::files.at(io::Active)==malformed);
    assert(save(broken,"{}",true)!=Error::None&&io::files.at(io::Active)==malformed);
    assert(save(broken,update,true)==Error::None&&broken.state()==State::Ready);io::intact();
  }
  std::cout<<"PASS corrupt/version/explicit replacement and unknown-password rejection\n";

  for(bool* fault:{&io::failOpenWrite,&io::shortWrite,&io::noSpace,&io::failSync,&io::failRename,&io::damageTemp,&io::alterTemp,&io::failWriteClose,&io::failTempOpen,&io::failTempRead}){
    io::reset();Store current;assert(save(current,update)==Error::None);
    auto old=io::files.at(io::Active);auto snapshot=*current.settings();
    *fault=true;assert(save(current,clear)!=Error::None);*fault=false;
    assert(io::files.at(io::Active)==old&&equal(*current.settings(),snapshot));
    Store restarted;assert(restarted.load(e)==State::Ready&&equal(*restarted.settings(),snapshot));io::intact();
  }
  for(bool* fault:{&io::probeError,&io::failOpenRead,&io::failRead,&io::earlyEof,&io::failStat,&io::failClose}){
    io::reset();Store current;assert(save(current,update)==Error::None);auto old=io::files.at(io::Active);
    *fault=true;assert(current.load(e)==State::IoError&&!current.settings());
    assert(save(current,omit,true)==Error::PasswordRequired);assert(io::files.at(io::Active)==old);
    assert(save(current,update,true)==Error::Io&&io::files.at(io::Active)==old);
    *fault=false;assert(current.load(e)==State::Ready);io::intact();
  }
  io::reset();io::mounted=false;Store unmounted;assert(unmounted.load(e)==State::IoError);
  assert(save(unmounted,update,true)==Error::Io&&io::writes==0);
  io::mounted=true;io::files[io::Temp]="interrupted write";
  assert(unmounted.load(e)==State::Missing&&io::files.count(io::Active)==0);
  assert(save(unmounted,update)==Error::None);io::intact();
  // Corruption after a successful load cannot be silently overwritten by stale RAM.
  io::files[io::Active]="broken";assert(save(unmounted,update)==Error::ReplaceRequired&&!unmounted.settings());
  assert(save(unmounted,clear,true)==Error::None&&!unmounted.settings()->password[0]);
  std::cout<<"PASS I/O/write/full/sync/rename/verify failures, interrupted tmp, Config isolation\n";
}
