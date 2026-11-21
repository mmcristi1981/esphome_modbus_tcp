// Compile the production transport against controlled socket/network/clock shims.
#include "modbus.h"
#include "esphome/components/network/util.h"
#include <cassert>
#include <iostream>
#include <limits>
namespace esphome { uint32_t test_clock=0; namespace network { bool available=false; } }
namespace fake {
int allocations=0, alive=0, attempts=0, connect_result=0, socket_error=0, ready=1, write_limit=260;
bool eof=false, write_blocked=false;
std::deque<std::vector<uint8_t>> reads;
std::vector<uint8_t> writes;
}
using namespace esphome;
using namespace esphome::modbustcp;
struct Device : ModbusDevice {
  int responses=0, exceptions=0;
  std::vector<uint8_t> last;
  void on_modbus_data(const std::vector<uint8_t>& d) override { ++responses; last=d; }
  void on_modbus_error(uint8_t f,uint8_t e) override { assert(f==3 && e==2); ++exceptions; }
};
void reset() {
  assert(fake::alive==0);
  fake::allocations=fake::attempts=0;
  fake::connect_result=fake::socket_error=0;
  fake::ready=1; fake::write_limit=260;
  fake::eof=fake::write_blocked=false;
  fake::reads.clear(); fake::writes.clear();
  network::available=true; test_clock=0;
}
void init(Modbus& m,Device& d) {
  m.set_tcp_or_rtu(true); m.set_host("192.0.2.1"); m.set_port(502);
  d.set_address(1); d.set_parent(&m); m.register_device(&d); m.setup();
}
std::vector<uint8_t> reply(int value=1234) {
  assert(fake::writes.size()>=12);
  return {fake::writes[0],fake::writes[1],0,0,0,7,1,3,4,0,0,uint8_t(value>>8),uint8_t(value)};
}
void request(Modbus& m) { fake::writes.clear(); m.send(1,3,32064,2); }
int main() {
  reset(); {
    Modbus m; Device d; init(m,d); network::available=false;
    for(test_clock=0;test_clock<120000;test_clock+=10) m.loop();
    assert(fake::allocations==0 && fake::attempts==0);
    network::available=true; m.loop(); assert(fake::attempts==1 && fake::alive==1);
    request(m); assert(fake::writes==std::vector<uint8_t>({0,1,0,0,0,6,1,3,0x7d,0x40,0,2}));
    auto r=reply();
    for(auto b:r) { fake::reads.push_back({b}); m.loop(); }
    assert(d.responses==1 && d.last==std::vector<uint8_t>({0,0,4,210}));
    std::cout<<"PASS delayed Wi-Fi startup and byte-at-a-time response\n";
  }
  reset(); {
    Modbus m; Device d; init(m,d); fake::connect_result=-1;
    for(test_clock=0;test_clock<100000;test_clock+=10) { m.loop(); assert(fake::alive==0); }
    assert(fake::attempts==20);
    fake::connect_result=0; m.loop(); request(m); fake::reads.push_back(reply()); m.loop();
    assert(d.responses==1);
    std::cout<<"PASS repeated failures release sockets and retain five-second backoff\n";
  }
  reset(); {
    Modbus m; Device d; init(m,d); fake::connect_result=1; fake::ready=0;
    m.loop(); for(test_clock=1;test_clock<5000;++test_clock) m.loop();
    assert(fake::attempts==1 && fake::alive==1); m.loop(); assert(fake::alive==0);
    test_clock=9999; m.loop(); assert(fake::attempts==1);
    test_clock=10000; m.loop(); assert(fake::attempts==2);
    fake::ready=1; fake::socket_error=ECONNREFUSED; m.loop(); assert(fake::alive==0);
    std::cout<<"PASS pending connect timeout and asynchronous refusal\n";
  }
  reset(); {
    Modbus m; Device d; init(m,d); m.loop(); request(m); auto old=reply();
    test_clock+=251; m.loop(); assert(!m.waiting_for_response_);
    request(m); auto current=reply(4321);
    old.insert(old.end(),current.begin(),current.end()); fake::reads.push_back(old); m.loop();
    assert(d.responses==1 && d.last==std::vector<uint8_t>({0,0,16,225}));
    fake::reads.push_back(current); m.loop(); assert(d.responses==1);
    request(m); auto ex=std::vector<uint8_t>{fake::writes[0],fake::writes[1],0,0,0,3,1,0x83,2};
    fake::reads.push_back(ex); m.loop(); assert(d.exceptions==1 && !m.waiting_for_response_);
    std::cout<<"PASS coalesced stale/current replies, unsolicited replies and exceptions\n";
  }
  reset(); {
    Modbus m; Device d; init(m,d); m.loop(); fake::write_limit=3;
    request(m); assert(fake::writes.size()==3);
    for(int i=0;i<3;++i) m.loop(); assert(fake::writes.size()==12);
    fake::reads.push_back(reply()); m.loop(); assert(d.responses==1);
    fake::write_blocked=true; request(m); test_clock+=5000; m.loop(); assert(fake::alive==0);
    std::cout<<"PASS partial writes and stalled writer timeout\n";
  }
  reset(); {
    Modbus m; Device d; init(m,d); m.loop();
    for(int i=0;i<3;++i) { request(m); test_clock+=251; m.loop(); }
    assert(fake::alive==0); test_clock+=5000; m.loop(); assert(fake::alive==1);
    request(m); fake::reads.push_back(reply()); m.loop(); assert(d.responses==1);
    network::available=false; m.loop(); assert(fake::alive==0);
    network::available=true; test_clock+=5000; m.loop(); assert(fake::alive==1);
    fake::eof=true; m.loop(); assert(fake::alive==0);
    std::cout<<"PASS silent inverter, Wi-Fi disconnect and remote close recovery\n";
  }
  for(int type=0;type<4;++type) { reset();
    Modbus m; Device d; init(m,d); m.loop(); request(m); auto r=reply();
    if(type==0) r[2]=1;
    if(type==1) {r[4]=1;r[5]=255;}
    if(type==2) r[8]=3;
    if(type==3) r[7]=4;
    fake::reads.push_back(r); m.loop(); assert(d.responses==0 && fake::alive==0);
  }
  std::cout<<"PASS invalid protocol, length, byte count and function rejected\n";
  reset(); {
    Modbus m; Device d; init(m,d); test_clock=std::numeric_limits<uint32_t>::max()-2000;
    fake::connect_result=-1; m.loop(); test_clock+=4999; m.loop(); assert(fake::attempts==1);
    test_clock+=1; m.loop(); assert(fake::attempts==2);
    std::cout<<"PASS retry timer across millis wraparound\n";
  }
  assert(fake::alive==0);
}
