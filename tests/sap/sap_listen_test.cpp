#include "rtp/sap.h"
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
using namespace pan::rtp;
int main(){
  SapListener l; assert(l.Start("lo0", 60));
  std::mutex m; std::vector<SapSession> v;
  SdpSession ss; ss.sName="Loop 8ch"; ss.nChannels=8; ss.vLegs={{"","239.69.145.10","127.0.0.1",5004}};
  { std::lock_guard g(m); v={{"lo0","127.0.0.1",ss}}; }
  SapAnnouncer a; a.Start([&]{ std::lock_guard g(m); return v; }, 30, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  auto d=l.List(); assert(d.size()==1 && d[0].sName=="Loop 8ch" && d[0].nVersion==1);
  auto key=d[0].sKey; auto sdp=l.FindSdp(key); assert(sdp && ParseSdp(*sdp)->nChannels==8);
  printf("discovered key=[%s] v%llu\n", key.c_str(), (unsigned long long)d[0].nVersion);
  { std::lock_guard g(m); v[0].session.nChannels=2; }
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  d=l.List(); assert(d.size()==1 && d[0].sKey==key && d[0].nVersion==2 && ParseSdp(*l.FindSdp(key))->nChannels==2);
  printf("update replaced same key, v%llu, 2ch\n", (unsigned long long)d[0].nVersion);
  // garbage packets ignored
  uint8_t junk[]={0xff,0,0,0,1,2,3,4,'x'}; assert(!l.HandlePacket(junk,sizeof junk));
  { std::lock_guard g(m); v.clear(); }
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  assert(l.List().empty()); printf("deletion removed it\nALL OK\n");
  a.Stop(); l.Stop();
}
