#include <cstdlib>
#include <array>
#include <cstdio>
#include <defhelper.hpp>
#include <run.hpp>
#include "header.hpp"
TOP("./Top.hpp");
PROJECT(".");
REQUEST(submit1, dims=[8], ARG(uint32_t) data);
REQUEST(submit2, dims=[4], ARG(uint32_t) data);
QUERY(snapshot, ArraySnapshot);
SIMULATION() {
    uint32_t expected1[8]; uint32_t expected2[4];
    for (uint32_t i = 0; i < 8; ++i) expected1[i] = 23 + ((i+1) << 16) + ((i+4) << 24) + 0x80000000u;
    for (uint32_t i = 0; i < 4; ++i) expected2[i] = 23 + (4 << 16) + (7 << 24) + 0x80000000u;
    for (uint32_t phase = 0; phase < 2; ++phase) {
        sim_reset();
        for (uint32_t cycle = 0; cycle < 20; ++cycle) {
            ArraySnapshot before = snapshot();
            for (uint32_t i = 0; i < 8; ++i) {
                if (before.first[i] != expected1[i]) { printf("first[%u] cycle=%u actual=%u expected=%u\n", i, cycle, before.first[i], expected1[i]); std::exit(1); }
            }
            for (uint32_t i = 0; i < 4; ++i) if (before.second[i] != expected2[i]) { printf("second[%u] cycle=%u actual=%u expected=%u\n", i, cycle, before.second[i], expected2[i]); std::exit(2); }
            const uint32_t data = 0xA1935u + cycle * 1031;
            std::array<uint32_t, 8> first_data{};
            std::array<uint32_t, 4> second_data{};
            for (uint32_t i = 0; i < 8; ++i) first_data[i] = data+i;
            for (uint32_t i = 0; i < 4; ++i) second_data[i] = data+20+i;
            submit1(first_data);
            submit2(second_data);
            sim_nextcycle();
            ArraySnapshot after = snapshot();
            for (uint32_t i = 0; i < 8; ++i) {
                if (after.streamed[i] != expected1[i]) { printf("streamed[%u] cycle=%u actual=%u expected=%u\n", i, cycle, after.streamed[i], expected1[i]); std::exit(3); }
                expected1[i] = ((data+i) & ((1u << (i+9))-1)) + ((i+1) << 16) + ((i+4) << 24) + (cycle < i ? 0x80000000u : 0u);
            }
            for (uint32_t i = 0; i < 4; ++i) expected2[i] = ((data+20+i) & 4095) + (4 << 16) + (7 << 24) + (cycle < 3 ? 0x80000000u : 0u);
        }
        for (uint32_t i = 0; i < 8; ++i) expected1[i] = 23 + ((i+1) << 16) + ((i+4) << 24) + 0x80000000u;
        for (uint32_t i = 0; i < 4; ++i) expected2[i] = 23 + (4 << 16) + (7 << 24) + 0x80000000u;
    }
    printf("array_instances PASS\n");
}
