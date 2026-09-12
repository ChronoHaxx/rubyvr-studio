// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_rules.h"
#include <algorithm>
namespace vr::world::live {
void capture_actor_rules(const Memory& m,Snapshot& s,uint64_t epoch) {
    s.actor_range_safe=false;s.actor_templates.clear();s.actor_epoch=epoch;
    if(!field_controls_available(m))return;
    const auto* header=m.read(kGMapHeader+4,4);
    if(!header)return;
    const uint32_t address=uint32_t(header[0])|uint32_t(header[1])<<8|uint32_t(header[2])<<16|uint32_t(header[3])<<24;
    const auto* events=m.read_rom(address,20);
    if(!events || events[0]>64)return;
    const auto* templates=m.read(kGSaveBlock1+0xc20,64*24);
    const auto* flags=m.read(kGSaveBlock1+0x1220,288);
    if(!templates || !flags)return;
    for(unsigned i=0;i<events[0];++i) {
        ActorTemplate t;std::copy_n(templates+i*24,24,t.bytes.begin());
        const unsigned flag=t.bytes[20]|unsigned(t.bytes[21])<<8;
        // Special flags are deliberately not inferred from a normal SaveBlock.
        t.hidden=flag>=288*8 || (flag && (flags[flag/8]&(1u<<(flag&7))));
        s.actor_templates.push_back(t);
    }
    s.actor_range_safe=true;
}
}
