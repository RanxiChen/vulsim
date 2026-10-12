// MIT License

// Copyright (c) 2025 Meng Chengzhen, in Shandong University

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

/**
 * The contents of this header file are only used to assist in syntax checking
 * and code completion during coding, and have no connection to the final
 * runtime semantics.
 */

#include <inttypes.h>
#include <fixint.hpp>
#include <ram.hpp>
#include <storage.hpp>
#include <queue.hpp>

#include <array>

inline constexpr int64_t clog2(int64_t value) {
    if (value <= 1) return 0;
    int64_t result = 0;
    int64_t threshold = 1;
    while (threshold < value) {
        threshold <<= 1;
        ++result;
    }
    return result;
}

// Editor declarations cover the positional scalar subset. Named declarations
// and bracketed dimension lists are checked by vul, not by the C++ preprocessor.
// No definitions here participate in simulation or RTL generation.
#include <type_traits>

#define HELPER() inline namespace helper
#define CONFIG(name, value) constexpr int64_t name = value;
#define PARAMETER(name, value) constexpr int64_t name = value;
#define ALIAS(name, type) using name = type;
#define STRUCT(name) struct name
#define ENUM(name) enum class name

// Scalar REGISTER supports an optional positional write-port count and an
// optional reset body. The trailing function declaration accepts either ';'
// or '{ ... }', keeping the reset object in its original element type.
#define VUL_IDE_REGISTER_SELECT(_1, _2, _3, macro, ...) macro
#define VUL_IDE_REGISTER_2(name, type) VUL_IDE_REGISTER_3(name, type, 1)
#define VUL_IDE_REGISTER_3(name, type, ports) \
    VulRegister<type, ports> name; void __##name##_reset(type& name)
#define REGISTER(...) \
    VUL_IDE_REGISTER_SELECT(__VA_ARGS__, VUL_IDE_REGISTER_3, VUL_IDE_REGISTER_2)(__VA_ARGS__)
#define WIRE(name, type) type name; void __##name##_reset(type& name)

#define ARG(type) const type &
#define RESP(type) type &

// Detect a positional handshake literal before the ARG/RESP sequence. Omitted
// handshake keeps REQUEST(name, ARG(T) x) usable without an extra API name.
#define VUL_IDE_CAT_IMPL(a, b) a##b
#define VUL_IDE_CAT(a, b) VUL_IDE_CAT_IMPL(a, b)
#define VUL_IDE_FIRST(first, ...) first
#define VUL_IDE_SECOND(first, second, ...) second
#define VUL_IDE_PROBE(...) VUL_IDE_SECOND(__VA_ARGS__, 0)
#define VUL_IDE_HS_0 ~, 1
#define VUL_IDE_HS_1 ~, 1
#define VUL_IDE_HS_false ~, 1
#define VUL_IDE_HS_true ~, 1
#define VUL_IDE_IS_HS(first) VUL_IDE_PROBE(VUL_IDE_CAT(VUL_IDE_HS_, first))
#define VUL_IDE_REQUEST_0(name, ...) template<uint32_t... IDX> void name(__VA_ARGS__);
#define VUL_IDE_REQUEST_1(name, handshake, ...) \
    template<uint32_t... IDX> std::conditional_t<(handshake), bool, void> name(__VA_ARGS__);
#define REQUEST(name, ...) \
    VUL_IDE_CAT(VUL_IDE_REQUEST_, VUL_IDE_IS_HS(VUL_IDE_FIRST(__VA_ARGS__)))(name __VA_OPT__(,) __VA_ARGS__)
#define VUL_IDE_SERVICE_0(name, ...) template<uint32_t IDX = 0> void __##name(__VA_ARGS__)
#define VUL_IDE_SERVICE_1(name, handshake, ...) VUL_IDE_SERVICE_0(name, __VA_ARGS__)
#define SERVICE(name, ...) \
    VUL_IDE_CAT(VUL_IDE_SERVICE_, VUL_IDE_IS_HS(VUL_IDE_FIRST(__VA_ARGS__)))(name __VA_OPT__(,) __VA_ARGS__)
#define QUERY(name, type) type name()
#define TICK_IMPL() void tick()

// Child aliases are supplied with their declared business signature for editor
// completion. Their actual handshake/type compatibility is checked by vul.
#define CHILD_INSTANCE(module, name) void* name = nullptr;
#define USE_CHILD_SERVICE(instance, service, alias, ...) template<uint32_t... IDX> bool alias(__VA_ARGS__);
#define USE_CHILD_QUERY(instance, query, alias, type) template<uint32_t... IDX> type alias();
#define CONNECT_CR_CS(srcchild, srcreq, dstchild, dstserv)
#define CONNECT_CR_S(srcchild, srcreq, dstserv)
#define CONNECT_CR_R(srcchild, srcreq, dstreq)
#define CONNECT_S_CS(srcserv, dstchild, dstserv)

#define VUL_IDE_BRAM_SELECT(_1, _2, _3, _4, _5, _6, macro, ...) macro
#define VUL_IDE_BRAM_3(name, type, size) VUL_IDE_BRAM_5(name, type, size, 1, 1)
#define VUL_IDE_BRAM_4(name, type, size, read_ports) VUL_IDE_BRAM_5(name, type, size, read_ports, 1)
#define VUL_IDE_BRAM_5(name, type, size, read_ports, write_ports) \
    VulBRAM<type, size, read_ports, write_ports> name;
#define VUL_IDE_BRAM_6(name, type, size, read_ports, write_ports, mode) \
    std::conditional_t<(#mode[0] == '1'), VulBRAM1RW<type, size>, VulBRAM<type, size, read_ports, write_ports>> name;
#define BRAM(...) \
    VUL_IDE_BRAM_SELECT(__VA_ARGS__, VUL_IDE_BRAM_6, VUL_IDE_BRAM_5, VUL_IDE_BRAM_4, VUL_IDE_BRAM_3)(__VA_ARGS__)
#define ROM(name, data_width, size, read_ports, init) VulROM<data_width, size, read_ports> name(init);
#define VUL_IDE_QUEUE_SELECT(_1, _2, _3, _4, _5, macro, ...) macro
#define VUL_IDE_QUEUE_3(name, type, depth) VulQueue<type, depth> name;
#define VUL_IDE_QUEUE_4(name, type, depth, enq_width) VUL_IDE_QUEUE_5(name, type, depth, enq_width, 1)
#define VUL_IDE_QUEUE_5(name, type, depth, enq_width, deq_width) \
    std::conditional_t<(enq_width == 1 && deq_width == 1), VulQueue<type, depth>, VulQueueMP<type, depth, enq_width, deq_width>> name;
#define QUEUE(...) \
    VUL_IDE_QUEUE_SELECT(__VA_ARGS__, VUL_IDE_QUEUE_5, VUL_IDE_QUEUE_4, VUL_IDE_QUEUE_3)(__VA_ARGS__)

// Version scopes intentionally contain no editor-side implementation selection.
#define INTERFACE() inline namespace vul_interface
#define VERSION(name) namespace name
#define USE_VERSION(name) namespace name {} using namespace name;
