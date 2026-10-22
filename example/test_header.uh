
#pragma once

#include <defhelper.hpp>

ALIAS(AESData, uint8_t, dims=[16]);
ALIAS(AESKey, uint8_t, dims=[16]);

CONFIG(WAY, 4);
CONFIG(ADDRW, 8);
CONFIG(DATW, 64);
CONFIG(MEMSIZE, (1ULL << ADDRW) * WAY);

STRUCT(MemItem) {
    uint64_t addr;
    uint32_t tag;
    bool     valid;
};

ALIAS(MemWay, MemItem, dims=[WAY]);



