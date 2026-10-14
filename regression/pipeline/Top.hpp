INTERFACE() {
    SERVICE(push, ARG(Packet) x, ARG(bool) valid);
    QUERY(result, Packet);
    QUERY(valid, bool);
    QUERY(lanes, Lanes);
}
USE_VERSION(selected);
VERSION(selected) {
    HELPER() { inline int16_t negate(int16_t x) { return -x; } }
    PIPELINE(transform, LATENCY,
        IREG(input, Packet, 2) { input.bits=7; input.delta=int16_t(0xfff3u); input.lanes=Lanes{11,29}; },
        IVALID(ivalid, 2),
        OREG(output, Packet),
        OVALID(name=ovalid)
    ) {
        output.bits = mix(input.bits);
        output.delta = negate(input.delta);
        output.lanes[0] = input.lanes[1];
        output.lanes[1] = input.lanes[0];
        if (!ivalid) output.bits = output.bits ^ Int<32>(255);
    }
    PIPELINE(copy, LATENCY,
        IREG(name=lane_in, type=LanePair),
        OREG(name=lane_out, type=LanePair)) {
        for (uint32_t i=0; i<2; ++i) lane_out.values[i]=lane_in.values[i]+1;
    }
    SERVICE(push, ARG(Packet) x, ARG(bool) valid) {
        input.setnext<1>(Packet{});
        input.setnext<0>(x);
        ivalid.setnext<1>(!valid);
        ivalid.setnext<0>(valid);
        lane_in.setnext(LanePair{x.lanes});
    }
    QUERY(result, Packet) { return output.get(); }
    QUERY(valid, bool) { return ovalid; }
    QUERY(lanes, Lanes) { return lane_out.get().values; }
}
