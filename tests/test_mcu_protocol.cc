#include <cassert>
#include <cstdio>
#include <vector>
#include "mcu_protocol.h"
#include "uart1_communicator.h"
static std::vector<uint8_t> frame(uint8_t cmd,const std::vector<uint8_t>& p){
    std::vector<uint8_t> f={0x55,0xaa,3,cmd,uint8_t(p.size()>>8),uint8_t(p.size())};
    f.insert(f.end(),p.begin(),p.end());uint8_t sum=0;for(auto b:f)sum+=b;f.push_back(sum);return f;
}
int main(){
    auto& p=McuProtocol::GetInstance();std::vector<McuFunctionData> got;int heart=-1;
    p.SetFunctionDataCallback([&](const McuFunctionData& d){got.push_back(d);});
    p.SetHeartbeatCallback([&](uint8_t h){heart=h;});
    auto f=frame(7,{3,2,0,4,0x12,0x34,0x56,0x78});
    for(size_t split=0;split<=f.size();++split){
        p.ResetParser();got.clear();
        auto count=p.ParseMcuData(f.data(),split)+p.ParseMcuData(f.data()+split,f.size()-split);
        assert(count==1&&got.size()==1&&got[0].value==0x12345678);
    }
    p.ResetParser();got.clear();uint8_t noise[]={9,8,0x55};
    assert(p.ParseMcuData(noise,sizeof noise)==0);
    assert(p.ParseMcuData(f.data()+1,f.size()-1)==1&&got.size()==1);
    p.ResetParser();got.clear();auto bad=f;bad.back()^=1;bad.insert(bad.end(),f.begin(),f.end());
    assert(p.ParseMcuData(bad.data(),bad.size())==1&&got.size()==1);
    std::vector<uint8_t> oversized={0x55,0xaa,3,7,0xff,0xff,0};oversized.insert(oversized.end(),f.begin(),f.end());
    got.clear();assert(p.ParseMcuData(oversized.data(),oversized.size())==1&&got.size()==1);
    auto joined=f;joined.insert(joined.end(),f.begin(),f.end());got.clear();
    assert(p.ParseMcuData(joined.data(),joined.size())==2&&got.size()==2);
    auto hb=frame(0,{1});assert(p.ParseMcuData(hb.data(),hb.size())==1&&heart==1);
    auto malformed=frame(7,{3,2,0,4,1});got.clear();p.ParseMcuData(malformed.data(),malformed.size());assert(got.empty());
    auto unsupported=frame(7,{3,0x7f,0,1,9,1,1,0,1,1});
    p.ParseMcuData(unsupported.data(),unsupported.size());assert(got.size()==1&&got[0].value==1);
    assert(p.SendValue(3,0x12345678));auto tx=Uart1Communicator::sent;
    assert(tx.size()==15&&tx[2]==0&&tx[3]==6&&tx[10]==0x12&&tx[13]==0x78);
    uint8_t sum=0;for(size_t i=0;i+1<tx.size();++i)sum+=tx[i];assert(sum==tx.back());
    assert(!p.SendBool(0,true));Uart1Communicator::short_write=true;assert(!p.SendBool(1,true));
    puts("PASS MCU protocol: all split points, noise, checksum/length recovery, concatenation, TLV, big endian, short TX");
}
