CONFIG(LATENCY, 3);
ALIAS(Lanes, uint16_t, dims=[2]);
STRUCT(LanePair) { Lanes values; };
STRUCT(Packet) { Int<32> bits; int16_t delta; Lanes lanes; };
HELPER() {
    inline Int<32> mix(const Int<32>& x) {
        Int<32> a = Int<32>(x * Int<32>(3));
        return Int<32>((a << Int<5>(2)) ^ (x >> Int<5>(1))) + Int<32>(17);
    }
}
