#include <cstdlib>
#include <cstdio>
#include <vector>
#include <run.hpp>
TOP("Top.hpp"); PROJECT(".");
REQUEST(push, ARG(Packet) x, ARG(bool) valid);
QUERY(result, Packet); QUERY(valid, bool); QUERY(lanes, Lanes);
SIMULATION() {
    // Independent unsigned C++ oracle: do not call the hardware mix helper.
    auto expected = [](const Packet& x, bool v) {
        static const uint16_t table[8] = {3, 251, 4099, 65521, 17, 8191, 127, 32749};
        uint32_t state=x.bits.to<uint32_t>();
        if (!v) state^=0x13579bdfu;
        state^=0x9e3779b9u;
        for (uint32_t r=0; r<6; ++r) {
            state=state*(r*2+3)+(0x01020304u+r*17);
            uint32_t shift=(((state>>28)^r)&7u)+1;
            state=(state<<shift)|(state>>(32-shift));
            state^=table[((state>>8)+r)&7u];
            if ((state&15u)>7) state+=state>>11;
            else state=(state^(state<<5))-(r+1);
            state+=(state>>8)&65535u;
        }
        uint32_t patched=state ^ ((uint32_t(x.lanes[0])<<16)|x.lanes[1]);
        uint32_t index=x.lanes[0]&7u;
        uint32_t nibble=(state>>((index^7u)*4))&15u;
        patched=(patched&~(15u<<(index*4)))|(nibble<<(index*4));
        bool parity=false;
        for (uint32_t bits=patched; bits; bits>>=1) parity^=bool(bits&1);
        uint32_t carry=(state&65535u)+x.lanes[0];
        uint32_t word=parity ? patched+carry : patched-carry;
        if (!v) word^=255u;
        // Spell arithmetic right shift without relying on signed-shift behavior.
        int32_t delta=x.delta;
        int32_t shifted=delta>=0 ? delta/8 : -((-delta+7)/8);
        int32_t bias=shifted+int32_t(x.lanes[0])-17;
        Packet y{};
        y.bits=word;
        y.delta=int16_t(x.delta<0 ? -bias : bias);
        y.lanes=Lanes{uint16_t((state&65535u)+x.lanes[1]), uint16_t((word>>16)^x.lanes[0])};
        return y;
    };
    auto sample = [](uint32_t i) {
        static const uint32_t edges[8]={0,0xffffffffu,0x80000000u,0x7fffffffu,1,0x55555555u,0xaaaaaaaau,0x12345678u};
        static const int16_t signed_edges[8]={-32768,32767,-1,0,1,-99,99,-12345};
        Packet p{};
        p.bits=i%4==0 ? edges[(i/4)%8] : i*912367u+55u;
        p.delta=i%3==0 ? signed_edges[(i/3)%8] : int16_t(uint16_t(i*3137u+19u));
        p.lanes=Lanes{uint16_t(i*7919u), uint16_t(i*32749u+65535u)};
        return p;
    };
    struct Snapshot { Packet input; Lanes copy_input; bool valid; };
    uint32_t verified=0, bubbles=0;
    for (uint32_t pass=0; pass<2; ++pass) {
        sim_reset();
        Snapshot current{};
        current.input.bits=7; current.input.delta=-13; current.input.lanes=Lanes{11,29};
        const Packet reset_value=expected(current.input, false);
        auto initial=result();
        if (initial.bits!=reset_value.bits || initial.delta!=reset_value.delta ||
            initial.lanes!=reset_value.lanes || valid() || lanes()!=Lanes{1,1}) {
            printf("PIPELINE RESET FAIL latency=%d word=%u expected=%u delta=%d expected=%d lanes=%u,%u expected=%u,%u valid=%d\n",
                   int(LATENCY),initial.bits.to<uint32_t>(),reset_value.bits.to<uint32_t>(),int(initial.delta),int(reset_value.delta),
                   unsigned(initial.lanes[0]),unsigned(initial.lanes[1]),unsigned(reset_value.lanes[0]),unsigned(reset_value.lanes[1]),valid());
            std::exit(2);
        }
        std::vector<Snapshot> history;
        auto step = [&](uint32_t tag, bool v, bool send=true) {
            if (send) {
                current.input=sample(tag); current.copy_input=current.input.lanes; current.valid=v;
                push(current.input,v);
            }
            history.push_back(current);
            sim_nextcycle();
            int source=int(history.size())-1-int(LATENCY);
            bool want_valid=source>=0 && history[source].valid;
            Lanes want_lanes{1,1};
            if (source>=0) want_lanes=Lanes{uint16_t(history[source].copy_input[0]+1),uint16_t(history[source].copy_input[1]+1)};
            if (valid()!=want_valid || lanes()!=want_lanes) {
                printf("PIPELINE ALIGN FAIL pass=%u cycle=%zu source=%d latency=%d valid=%d expected=%d\n",pass,history.size()-1,source,int(LATENCY),valid(),want_valid);
                std::exit(3);
            }
            if (want_valid) {
                Packet want=expected(history[source].input,true), got=result();
                if (got.bits!=want.bits || got.delta!=want.delta || got.lanes!=want.lanes) {
                    printf("PIPELINE DATA FAIL pass=%u cycle=%zu source=%d latency=%d word=%u expected=%u delta=%d expected=%d lanes=%u,%u expected=%u,%u\n",
                           pass,history.size()-1,source,int(LATENCY),got.bits.to<uint32_t>(),want.bits.to<uint32_t>(),int(got.delta),int(want.delta),
                           unsigned(got.lanes[0]),unsigned(got.lanes[1]),unsigned(want.lanes[0]),unsigned(want.lanes[1]));
                    std::exit(1);
                }
                ++verified;
            } else ++bubbles; // Invalid data intentionally has no oracle assertion.
        };
        uint32_t tag=pass*1000;
        for (uint32_t i=0;i<LATENCY+3;++i) step(tag++,false); // Initial bubbles.
        for (uint32_t i=0;i<2*LATENCY+24;++i) step(tag++,true); // Full-rate burst.
        for (uint32_t i=0;i<40;++i) step(tag++,i%2==0); // Alternating bubbles.
        for (uint32_t i=0;i<56;++i) step(tag++,i%7!=3); // Single holes in bursts.
        for (uint32_t i=0;i<2*LATENCY+9;++i) step(tag++,false); // Empty the entire valid chain.
        for (uint32_t i=0;i<88;++i) step(tag++,i%11==0); // Isolated transactions.
        uint32_t random=0x9e3779b9u+pass;
        for (uint32_t i=0;i<192;++i) {
            random=random*1664525u+1013904223u;
            step(tag++,bool(random&0x80000000u));
        }
        step(tag++,true);
        for (uint32_t i=0;i<LATENCY+4;++i) step(tag,true,false); // Unwritten valid holds true.
        step(tag++,false);
        for (uint32_t i=0;i<LATENCY+4;++i) step(tag,false,false); // Unwritten valid holds false.
        // Reset with a full chain of known valid transactions still in flight.
        for (uint32_t i=0;i<LATENCY+2;++i) step(tag++,true);
    }
    if (verified<200 || bubbles<200) std::exit(4);
    printf("PIPELINE PASS latency=%d valid_checks=%u bubble_checks=%u\n",int(LATENCY),verified,bubbles);
}
