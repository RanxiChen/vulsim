CONFIG(LATENCY, 3);
ALIAS(Lanes, uint16_t, dims=[2]);
STRUCT(LanePair) { Lanes values; };
STRUCT(Packet) { Int<32> bits; int16_t delta; Lanes lanes; };
HELPER() {
    inline Int<32> mix(const Int<32>& x) {
        static const uint16_t table[8] = {3, 251, 4099, 65521, 17, 8191, 127, 32749};
        Int<32> state = x ^ Int<32>(0x9e3779b9u);
        // Nonlinear rounds keep a deep dependency chain after optimization.
        for (uint32_t round=0; round<6; ++round) {
            state = state * Int<32>(round*2+3) + Int<32>(0x01020304u+round*17);
            Int<4> head = state.at<31,28>();
            uint32_t shift = ((head.to<uint32_t>() ^ round) & 7u) + 1;
            state = (state << shift) | (state >> (32u-shift));
            Int<3> selector = state.at<10,8>();
            uint32_t index = (selector.to<uint32_t>() + round) & 7u;
            state = state ^ Int<32>(table[index]);
            if (state.at<3,0>() > Int<4>(7)) state = state + (state >> 11u);
            else state = (state ^ (state << 5u)) - Int<32>(round+1);
            state = state + Int<32>(state.at<23,8>());
        }
        return state;
    }
}
