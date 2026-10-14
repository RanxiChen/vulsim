#pragma once

#include "header.hpp"
#include <defhelper.hpp>

INTERFACE() {
    REQUEST(s3output, ARG(uint64_t) y);
    SERVICE(s0input, ARG(uint32_t) a, ARG(uint32_t) b);
}

STRUCT(S0S1RegData) {
    uint32_t a;
    uint32_t b;
};

STRUCT(S1S2RegData) {
    Int<32> p0; // a_lo * b_lo
    Int<32> p1; // a_lo * b_hi
    Int<32> p2; // a_hi * b_lo
    Int<32> p3; // a_hi * b_hi
};

STRUCT(S2S3RegData) {
    Int<16> low16;
    Int<34> mid;
    Int<32> high_base;
};

// Two cycles from input Q to output Q retain the existing callback at tick + 3.
// RTLZZ chooses the internal cut; the expanded multiplication tree stays intact.
PIPELINE(multiply, 2,
    IREG(s1reg, S0S1RegData),
    IVALID(s1valid, 2),
    OREG(s3reg, uint64_t),
    OVALID(s3valid)
) {
    // Calculate the four 16 x 16 bit partial products.
    Int<32> a = s1reg.a;
    Int<32> b = s1reg.b;
    Int<16> a_lo = a.at<15, 0>();
    Int<16> b_lo = b.at<15, 0>();
    Int<16> a_hi = a.at<31, 16>();
    Int<16> b_hi = b.at<31, 16>();

    S1S2RegData partial;
    partial.p0 = a_lo * b_lo;
    partial.p1 = a_lo * b_hi;
    partial.p2 = a_hi * b_lo;
    partial.p3 = a_hi * b_hi;

    // Keep only the carry widths needed to combine the cross products.
    S2S3RegData combined;
    combined.low16 = partial.p0.at<15, 0>();
    Int<32> p0_high = partial.p0.at<31, 16>();
    auto mid_low = AddCarry(p0_high, partial.p1); // Int<33>
    auto mid = AddCarry(mid_low, Int<33>(partial.p2)); // Int<34>
    combined.mid = mid;
    combined.high_base = partial.p3;

    // Combine the high partial product and assemble the 64-bit result.
    Int<32> mid_high = combined.mid.at<33, 16>();
    auto high = AddCarry(combined.high_base, mid_high); // Int<33>
    Int<64> y = Cat(high.at<31, 0>(), combined.mid.at<15, 0>(), combined.low16);
    s3reg = y.to<uint64_t>();
}

SERVICE(s0input, ARG(uint32_t) a, ARG(uint32_t) b) {
    s1reg.setnext(S0S1RegData{a, b});
    s1valid.setnext<0>(true);
}

TICK_IMPL() {
    // Clear valid unless this cycle accepts a new input; port 0 takes priority.
    s1valid.setnext<1>(false);
    if (s3valid) {
        s3output(s3reg.get());
    }
}
