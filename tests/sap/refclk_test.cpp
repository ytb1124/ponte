#include "rtp/sdp.h"
#include <cassert>
#include <cstdio>
using namespace pan::rtp;
int main(){ SdpSession s; s.sName="x"; s.sPtpGrandmaster="02:13:5a:ff:fe:e4:57:2f"; s.nPtpDomain=3; s.vLegs={{"","239.1.1.1","10.0.0.1",5004}};
 auto t=GenerateSdp(s,"10.0.0.1",{"10.0.0.1"},5,1); assert(t.find("ts-refclk:ptp=IEEE1588-2008:02-13-5A-FF-FE-E4-57-2F:3\r\n")!=std::string::npos);
 auto p=ParseSdp(t); assert(p && p->sPtpGrandmaster=="02-13-5A-FF-FE-E4-57-2F" && p->nPtpDomain==3); puts("refclk format OK"); }
