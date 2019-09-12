#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <cstdio>
#include <univang/format/buffer.hpp>
#include <univang/format/format.hpp>

static void BM_sprintf(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        // prints "1.2340000000:0042:+3.13:str:0x3e8:X:%"
        char buf[100];
        sprintf(
            buf, "%0.10f:%04d:%+g:%s:%p:%c:%%\n", 1.234, 42, 3.13, "str",
            (void*)1000, (int)'X');
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_libfmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        // prints "1.2340000000:0042:+3.13:str:0x3e8:X:%"
        benchmark::DoNotOptimize(fmt::format_to(
            buf, "{:.10f}:{:04}:{:+}:{}:{}:{}:%\n", 1.234, 42, 3.13, "str",
            (const void*)1000, 'X'));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_my_fmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        // prints "1.2340000000:0042:+3.13:str:0x00000000000003e8:X:%"
        benchmark::DoNotOptimize(univang::fmt::format_to(
            buf, "{:.10f}:{:04}:{:+}:{}:{}:{}:%\n", 1.234, 42, 3.13, "str",
            (const void*)1000, 'X'));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doublef_sprintf(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        sprintf(buf, "%0.10f", 0.0000012345);
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doublef_libfmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(fmt::format_to(buf, "{:.10f}", 0.0000012345));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doublef_my_fmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(
            univang::fmt::format_to(buf, "{:.10f}", 0.0000012345));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doubleg_sprintf(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        sprintf(buf, "%g", 143213413.00012345);
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doubleg_libfmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(fmt::format_to(buf, "{}", 143213413.00012345));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_doubleg_my_fmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(
            univang::fmt::format_to(buf, "{}", 143213413.000123455));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_uint_sprintf(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        sprintf(buf, "%1d", 12345678);
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_uint_libfmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(fmt::format_to(buf, "{:d}", 12345678));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_uint_my_fmt(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        benchmark::DoNotOptimize(
            univang::fmt::format_to(buf, "{:d}", 12345678));
        ++i;
    }
    state.SetItemsProcessed(i);
}

static void BM_str_sprintf(benchmark::State& state) {
    int64_t i = 0;
    for(auto _ : state) {
        char buf[100];
        sprintf(buf, "%s", "some string to format");
        ++i;
    }
    state.SetItemsProcessed(i);
}

// Register the function as a benchmark
BENCHMARK(BM_sprintf);
BENCHMARK(BM_libfmt);
BENCHMARK(BM_my_fmt);
BENCHMARK(BM_doublef_sprintf);
BENCHMARK(BM_doublef_libfmt);
BENCHMARK(BM_doublef_my_fmt);
BENCHMARK(BM_doubleg_sprintf);
BENCHMARK(BM_doubleg_libfmt);
BENCHMARK(BM_doubleg_my_fmt);
BENCHMARK(BM_uint_sprintf);
BENCHMARK(BM_uint_libfmt);
BENCHMARK(BM_uint_my_fmt);

BENCHMARK_MAIN();
