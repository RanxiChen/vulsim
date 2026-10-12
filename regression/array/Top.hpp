#pragma once
#include "header.hpp"
INTERFACE() {
    SERVICE(submit1, dims=[8], ARG(uint32_t) data);
    SERVICE(submit2, dims=[4], ARG(uint32_t) data);
    SERVICE(capture1, dims=[8], ARG(uint32_t) data);
    SERVICE(capture2, dims=[4], ARG(uint32_t) data);
    QUERY(snapshot, ArraySnapshot);
}
CHILD_INSTANCE(PE, arr1, dims=[8], PARAM(ROW)=COORD(0));
CHILD_INSTANCE(PE, arr2, dims=[4]);
CONNECT_S_CS(submit1, arr1[*], submit);
CONNECT_S_CS(submit2, arr2[*], submit);
CONNECT_CR_S(arr1[*], stream, capture1);
CONNECT_CR_S(arr2[*], stream, capture2);
USE_CHILD_QUERY(arr1[*], result, first, uint32_t);
USE_CHILD_QUERY(arr2[*], result, second, uint32_t);
REGISTER(last, uint32_t, dims=[8]);
SERVICE(capture1, dims=[8], ARG(uint32_t) data) { last.setnext(IDX, data); }
SERVICE(capture2, dims=[4], ARG(uint32_t) data) { }
QUERY(snapshot, ArraySnapshot) {
    ArraySnapshot s{};
    s.first = first();
    s.second = second();
    for (uint32_t i = 0; i < 8; ++i) s.streamed[i] = last[i];
    return s;
}
