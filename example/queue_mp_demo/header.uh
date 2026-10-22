#pragma once

#include <array>
#include <cstdint>
#include <defhelper.hpp>

ALIAS(U32x3, uint32_t, dims=[3]);
ALIAS(U32x2, uint32_t, dims=[2]);
CONFIG(Q_DEPTH, 4);
CONFIG(Q_ENQ_WIDTH, 3);
CONFIG(Q_DEQ_WIDTH, 2);

STRUCT(QueueMPStatus) {
    uint32_t enq_rdy;
    uint32_t deq_valid;
};
