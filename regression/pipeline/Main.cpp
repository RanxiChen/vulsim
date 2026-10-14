#include <cstdlib>
#include <cstdio>
#include <run.hpp>
TOP("Top.hpp"); PROJECT(".");
REQUEST(push, ARG(Packet) x, ARG(bool) valid);
QUERY(result, Packet); QUERY(valid, bool); QUERY(lanes, Lanes);
SIMULATION() {
    auto sample = [](uint32_t i) {
        Packet p{};
        p.bits = uint32_t(i*912367u+55u);
        p.delta = int16_t(int(i%199)-99);
        p.lanes = Lanes{uint16_t(i*3u), uint16_t(i*7u+9u)};
        return p;
    };
    auto check = [&](uint32_t i) {
        Packet x=sample(i);
        const bool v=(i%3!=0);
        uint32_t u=x.bits.to<uint32_t>();
        uint32_t expected=((uint32_t(u*3u)<<2)^(u>>1))+17u;
        if (!v) expected^=255u;
        auto y=result(); auto a=lanes();
        if (y.bits.to<uint32_t>()!=expected || y.delta!=-x.delta ||
            y.lanes!=Lanes{x.lanes[1],x.lanes[0]} || valid()!=v ||
            a!=Lanes{uint16_t(x.lanes[0]+1),uint16_t(x.lanes[1]+1)}) {
            printf("PIPELINE FAIL sample=%u latency=%d actual=%u expected=%u valid=%d\n",i,int(LATENCY),y.bits.to<uint32_t>(),expected,valid());
            std::exit(1);
        }
    };
    for (uint32_t pass=0; pass<2; ++pass) {
        sim_reset();
        auto reset_value=result();
        if (reset_value.bits.to<uint32_t>()!=151 || reset_value.delta!=13 || reset_value.lanes!=Lanes{29,11} || valid() || lanes()!=Lanes{1,1}) std::exit(2);
        for (uint32_t i=0; i<48+LATENCY; ++i) {
            push(sample(i), i%3!=0);
            sim_nextcycle();
            if (i>=LATENCY) check(i-LATENCY);
            else if (result().bits.to<uint32_t>()!=151 || valid() || lanes()!=Lanes{1,1}) std::exit(3);
        }
        // Unwritten inputs, including valid, hold their most recent value.
        for (uint32_t i=0; i<LATENCY+3; ++i) {
            sim_nextcycle();
            if (i>=LATENCY) check(47+LATENCY);
        }
    }
    printf("PIPELINE PASS latency=%d\n", int(LATENCY));
}
