INTERFACE() {
    SERVICE(push, ARG(Packet) x, ARG(bool) valid);
    QUERY(result, Packet);
    QUERY(valid, bool);
    QUERY(lanes, Lanes);
}
USE_VERSION(selected);
VERSION(selected) {
    HELPER() {
        inline int32_t signed_bias(int16_t x, uint16_t lane) {
            return (int32_t(x) >> 3) + int32_t(lane) - 17;
        }
    }
    PIPELINE(transform, LATENCY,
        IREG(input, Packet, 2) { input.bits=7; input.delta=int16_t(0xfff3u); input.lanes=Lanes{11,29}; },
        IVALID(ivalid, 2),
        OREG(output, Packet),
        OVALID(name=ovalid)
    ) {
        Int<32> seed = input.bits;
        if (!ivalid) seed = seed ^ Int<32>(0x13579bdfu);
        Int<32> mixed = mix(seed);
        Int<32> patched = mixed ^ Cat(Int<16>(input.lanes[0]), Int<16>(input.lanes[1]));
        uint32_t index = uint32_t(input.lanes[0]) & 7u;
        patched.pick<4>(index*4u) = mixed.pick<4>((index^7u)*4u);
        Int<16> low = mixed.at<15,0>();
        auto carry = AddCarry(low, Int<16>(input.lanes[0]));
        if (ReduceXor(patched)) output.bits = patched + Int<32>(carry);
        else output.bits = patched - Int<32>(carry);
        // Read valid again after the long path to exercise stage-local alignment.
        if (!ivalid) output.bits = output.bits ^ Int<32>(255);
        int32_t bias = signed_bias(input.delta, input.lanes[0]);
        output.delta = int16_t(input.delta < 0 ? -bias : bias);
        output.lanes[0] = uint16_t(low.to<uint32_t>() + input.lanes[1]);
        output.lanes[1] = uint16_t((output.bits.to<uint32_t>() >> 16) ^ input.lanes[0]);
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
