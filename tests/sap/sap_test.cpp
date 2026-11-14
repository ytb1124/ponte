#include "rtp/sap.h"
#include "rtp/sdp.h"
#include "compat/net_compat.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
using namespace pan::rtp;
struct Pkt { bool del; uint16_t hash; std::string origin, sdp; };
static bool Recv(int s, Pkt& p, int ms){
  timeval tv{ms/1000,(ms%1000)*1000}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
  uint8_t b[4096]; auto n=recv(s,b,sizeof b,0); if(n<=0) return false;
  assert((b[0]>>5)==1); assert(b[1]==0); p.del=(b[0]&0x04)!=0; p.hash=(b[2]<<8)|b[3];
  char ip[16]; inet_ntop(AF_INET,b+4,ip,16); p.origin=ip;
  const char* t=(const char*)b+8; assert(strcmp(t,"application/sdp")==0);
  size_t off=8+strlen(t)+1; p.sdp.assign((char*)b+off,n-off); return true; }
int main(){
  int s=socket(AF_INET,SOCK_DGRAM,0); int one=1; setsockopt(s,SOL_SOCKET,SO_REUSEPORT,&one,sizeof one);
  assert(pan::compat::BindUdp(s,9875,"239.255.255.255")); assert(pan::compat::JoinMulticast(s,"239.255.255.255","lo0"));
  std::mutex m; std::vector<SapSession> v;
  SdpSession ss; ss.sName="Test 8ch"; ss.nChannels=8; ss.sPtpGrandmaster="00-1D-C1-FF-FE-12-34-56"; ss.bClockDomain=true;
  ss.vLegs={{"","239.69.145.10","127.0.0.1",5004}};
  { std::lock_guard l(m); v={{"lo0","127.0.0.1",ss}}; }
  SapAnnouncer a; a.Start([&]{ std::lock_guard l(m); return v; }, 2, 1);
  Pkt p; assert(Recv(s,p,3000)); assert(!p.del); assert(p.origin=="127.0.0.1");
  printf("--- first announce (hash %04x) ---\n%s", p.hash, p.sdp.c_str());
  auto parsed=ParseSdp(p.sdp); assert(parsed && parsed->nChannels==8 && parsed->vLegs[0].sMulticast=="239.69.145.10" && parsed->nPtpDomain==0);
  uint16_t h1=p.hash; std::string o1=p.sdp.substr(0,p.sdp.find("\r\n",4));
  assert(Recv(s,p,3500)); assert(!p.del && p.hash==h1); printf("periodic re-announce ok, same hash\n");
  { std::lock_guard l(m); v[0].session.nChannels=2; }
  assert(Recv(s,p,3000)); assert(p.del && p.hash==h1); printf("change -> deletion of old (payload: %s)", p.sdp.c_str());
  assert(Recv(s,p,3000)); assert(!p.del && p.hash!=h1); 
  auto o2=p.sdp.substr(0,p.sdp.find("\r\n",4)); printf("change -> new announce, o-lines: [%s] -> [%s]\n", o1.c_str()+2, o2.c_str()+2);
  assert(p.sdp.find("L24/48000/2")!=std::string::npos);
  { std::lock_guard l(m); v.clear(); }
  assert(Recv(s,p,3000)); assert(p.del); printf("stream gone -> deletion ok\n");
  printf("status: %s\nALL OK\n", a.GetStatusJson().dump().c_str());
}
