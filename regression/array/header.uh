#pragma once
#include <defhelper.hpp>

CONFIG(SIZE, 4);

// Unsigned 8-bit operands; four products fit exactly in uint32_t (max 260100).
STRUCT(Operand) {
    uint8_t data;
    bool valid;
};
STRUCT(PEState) {
    uint32_t sum;
    uint32_t products;
};
ALIAS(MatrixState, PEState, dims=[SIZE, SIZE]);
