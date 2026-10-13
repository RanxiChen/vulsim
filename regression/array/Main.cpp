#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <defhelper.hpp>
#include <run.hpp>
#include "header.hpp"

TOP("./Top.hpp");
PROJECT(".");
REQUEST(left, dims=[SIZE], ARG(Operand) value);
REQUEST(up, dims=[SIZE], ARG(Operand) value);
REQUEST(clear);
QUERY(snapshot, MatrixState);

SIMULATION() {
    using Matrix = std::array<std::array<uint8_t, SIZE>, SIZE>;
    constexpr uint32_t seed = 0x4A17B29u;
    std::mt19937 rng(seed);
    uint32_t checks = 0;
    uint32_t cycles = 0;
    uint32_t job = 0;
    auto check = [&](const MatrixState &expected) {
        const auto actual = snapshot();
        for (uint32_t i = 0; i < SIZE; ++i) {
            for (uint32_t j = 0; j < SIZE; ++j) {
                ++checks;
                if (actual[i][j].sum != expected[i][j].sum ||
                    actual[i][j].products != expected[i][j].products) {
                    printf("array FAIL seed=%u job=%u cycle=%u PE[%u][%u] "
                           "sum=%u expected=%u products=%u expected=%u\n",
                           seed, job, cycles, i, j, actual[i][j].sum,
                           expected[i][j].sum, actual[i][j].products, expected[i][j].products);
                    std::exit(1);
                }
            }
        }
    };
    auto next = [&]() { sim_nextcycle(); ++cycles; };
    MatrixState empty{};
    sim_reset();
    check(empty);

    // The oracle uses C[i][j] = sum_k A[i][k]*B[k][j] and the injection
    // schedule, independently of the PE's forwarding/register implementation.
    auto run_job = [&](const Matrix &a, const Matrix &b, bool bubbles, bool scalar) {
        clear();
        next();
        MatrixState expected{};
        check(expected);
        std::array<int, SIZE> launch{};
        launch[0] = int(rng() % 4); // random leading idle time
        for (uint32_t k = 1; k < SIZE; ++k)
            launch[k] = launch[k-1] + 1 + (bubbles ? int(rng() % 4) : 0);
        const int last = launch[SIZE-1] + 2 * (SIZE-1) + 1;
        for (int t = 0; t <= last + 4; ++t) {
            std::array<Operand, SIZE> lhs{};
            std::array<Operand, SIZE> rhs{};
            for (uint32_t lane = 0; lane < SIZE; ++lane) {
                // Invalid tokens carry nonzero random data to test valid gating.
                lhs[lane].data = uint8_t(1 + rng() % 255);
                rhs[lane].data = uint8_t(1 + rng() % 255);
                for (uint32_t k = 0; k < SIZE; ++k) {
                    if (t == launch[k] + int(lane)) {
                        lhs[lane] = Operand{a[lane][k], true};
                        rhs[lane] = Operand{b[k][lane], true};
                    }
                }
            }
            if (scalar) {
                left<0>(lhs[0]); left<1>(lhs[1]); left<2>(lhs[2]); left<3>(lhs[3]);
                up<0>(rhs[0]); up<1>(rhs[1]); up<2>(rhs[2]); up<3>(rhs[3]);
            } else {
                left(lhs);
                up(rhs);
            }
            // A boundary token is registered first, then consumed one cycle later.
            for (uint32_t i = 0; i < SIZE; ++i) {
                for (uint32_t j = 0; j < SIZE; ++j) {
                    for (uint32_t k = 0; k < SIZE; ++k) {
                        if (t == launch[k] + int(i+j) + 1) {
                            expected[i][j].sum += uint32_t(a[i][k]) * uint32_t(b[k][j]);
                            ++expected[i][j].products;
                        }
                    }
                }
            }
            next();
            check(expected);
        }
        for (uint32_t i = 0; i < SIZE; ++i) {
            for (uint32_t j = 0; j < SIZE; ++j) {
                uint32_t reference = 0;
                for (uint32_t k = 0; k < SIZE; ++k)
                    reference += uint32_t(a[i][k]) * uint32_t(b[k][j]);
                if (expected[i][j].sum != reference || expected[i][j].products != SIZE)
                    std::exit(2);
            }
        }
        // Omitted boundary calls must insert bubbles and preserve finished results.
        for (uint32_t idle = 0; idle < 3; ++idle) { next(); check(expected); }
        ++job;
    };

    Matrix zero{}, identity{}, maximum{}, pattern{};
    for (uint32_t i = 0; i < SIZE; ++i) {
        identity[i][i] = 1;
        for (uint32_t j = 0; j < SIZE; ++j) {
            maximum[i][j] = 255;
            pattern[i][j] = uint8_t(1 + i*SIZE + j);
        }
    }
    run_job(zero, maximum, false, false);
    run_job(maximum, zero, true, true);
    run_job(identity, pattern, false, true);
    run_job(pattern, identity, true, false);
    run_job(maximum, maximum, false, false);
    // Single nonzero products expose swapped row/column and k indices.
    for (uint32_t i = 0; i < SIZE; ++i) {
        for (uint32_t j = 0; j < SIZE; ++j) {
            for (uint32_t k = 0; k < SIZE; ++k) {
                Matrix a{}, b{};
                a[i][k] = uint8_t(17+i+k);
                b[k][j] = uint8_t(29+j+k);
                run_job(a, b, true, (job & 1) != 0);
            }
        }
    }
    for (uint32_t trial = 0; trial < 256; ++trial) {
        Matrix a{}, b{};
        for (uint32_t i = 0; i < SIZE; ++i) {
            for (uint32_t j = 0; j < SIZE; ++j) {
                // Dense full-range, small, sparse, and 0/255 random distributions.
                const uint32_t x = rng(), y = rng();
                if (trial % 4 == 0) { a[i][j] = uint8_t(x); b[i][j] = uint8_t(y); }
                if (trial % 4 == 1) { a[i][j] = uint8_t(x % 8); b[i][j] = uint8_t(y % 8); }
                if (trial % 4 == 2) { a[i][j] = x % 3 ? 0 : uint8_t(x); b[i][j] = y % 3 ? 0 : uint8_t(y); }
                if (trial % 4 == 3) { a[i][j] = x & 1 ? 255 : 0; b[i][j] = y & 1 ? 255 : 0; }
            }
        }
        run_job(a, b, (trial & 1) != 0, trial % 3 == 0);
        if (trial % 16 == 0) {
            // Reset at several pipeline depths while nonzero operands are in flight.
            for (uint32_t t = 0; t < 1 + trial % 11; ++t) {
                std::array<Operand, SIZE> values{};
                for (auto &v : values) v = Operand{255, true};
                left(values); up(values); next();
            }
            sim_reset();
            check(empty);
            for (uint32_t t = 0; t < 10; ++t) { next(); check(empty); }
        }
    }
    // Asymmetric validity must not accumulate, even with nonzero invalid data.
    sim_reset();
    check(empty);
    for (uint32_t side = 0; side < 2; ++side) {
        std::array<Operand, SIZE> valid{}, invalid{};
        for (uint32_t lane = 0; lane < SIZE; ++lane) {
            valid[lane] = Operand{255, true};
            invalid[lane] = Operand{255, false};
        }
        for (uint32_t t = 0; t < 12; ++t) {
            if (t < 4) {
                left(side == 0 ? valid : invalid);
                up(side == 0 ? invalid : valid);
            }
            next();
            check(empty);
        }
    }
    // Only one boundary pair, followed by no calls at all: exactly one MAC.
    left<0>(Operand{2, true});
    up<0>(Operand{3, true});
    next();
    check(empty);
    MatrixState single{};
    single[0][0].sum = 6;
    single[0][0].products = 1;
    for (uint32_t t = 0; t < 12; ++t) { next(); check(single); }
    clear();
    next();
    check(empty);
    for (uint32_t t = 0; t < 10; ++t) { next(); check(empty); }

    printf("array PASS: %u matrices (256 random), %u cycles, %u PE checks, seed=%u\n",
           job, cycles, checks, seed);
}
