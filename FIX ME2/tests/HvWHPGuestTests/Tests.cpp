#include "../../apps/HvWHPGuest/GuestState.h"
#include <stdio.h>
int main() {
    using namespace hvwhp;
    int passed=0;
    Step s=Step::Empty;
    const Event events[]={Event::Created,Event::SetupDone,Event::MemoryMapped,Event::VcpuCreated,Event::RunStarted,Event::HltExit};
    const Step expected[]={Step::Partition,Step::Setup,Step::Memory,Step::Vcpu,Step::Executing,Step::Halted};
    for(unsigned i=0;i<sizeof(events)/sizeof(events[0]);++i){s=transition(s,events[i]);if(s!=expected[i]){puts("FAIL transition");return 1;}++passed;}
    if(!halted_successfully(s,0x1234)){puts("FAIL result");return 2;}++passed;
    if(halted_successfully(s,0x4321)){puts("FAIL bad result");return 3;}++passed;
    if(transition(Step::Empty,Event::RunStarted)!=Step::Failed){puts("FAIL invalid transition");return 4;}++passed;
    if(transition(Step::Executing,Event::Failure)!=Step::Failed){puts("FAIL fail path");return 5;}++passed;
    if(transition(Step::Failed,Event::Close)!=Step::Closed){puts("FAIL cleanup path");return 6;}++passed;
    if(sizeof(guest_code)!=4 || guest_code[0]!=0xB8 || guest_code[3]!=0xF4 || guest_memory_bytes != 4096){puts("FAIL guest bytes");return 7;}++passed;
    printf("PASS %d/%d portable WHP state tests (not a Windows hypervisor run).\n",passed,passed);
    return 0;
}
