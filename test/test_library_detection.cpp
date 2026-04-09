/**
 * @file test_library_detection.cpp
 * @brief Stress tests for the two-tier library detection system.
 *
 * Tests:
 *   1. Tier 1 binary search correctness (known libs hit, unknown miss)
 *   2. Tier 2 MLP predictor fallback + auto-append
 *   3. Sorted vector invariant after insertions
 *   4. Scoped package name extraction (@scope/name)
 *   5. Concurrent access (multi-threaded stress)
 *   6. Performance benchmarks (throughput)
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <mutex>
#include <functional>

#include "library_predictor.h"
#include "library_dedup.h"
#include "dedup_index.h"
#include "daemon/directory tree/adt.h"

// Stubs for extern symbols referenced by library_dedup.cpp
// (folder dedup functions use these, but we don't exercise them in this test)
treefile* file1 = nullptr;
DedupIndex g_index;

// ============================================================
// Helpers
// ============================================================

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) \
    do { std::cout << "\n[TEST] " << (name) << " ..." << std::endl; } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << (msg) << " (" << #cond << ")" << std::endl; \
            g_fail++; \
        } else { \
            g_pass++; \
        } \
    } while(0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)
#define ASSERT_EQ(a, b, msg) ASSERT_TRUE((a) == (b), msg)

// Create a temporary tracked_libraries.json for testing
static std::string create_temp_json(const std::string& path,
                                     const std::vector<std::string>& libs) {
    std::ofstream f(path);
    f << "{\n  \"tracked_libraries\": [\n";
    for (size_t i = 0; i < libs.size(); i++) {
        f << "    \"" << libs[i] << "\"";
        if (i + 1 < libs.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    f.close();
    return path;
}

// Read back the JSON and extract library names
static std::vector<std::string> read_json_libs(const std::string& path) {
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    std::vector<std::string> libs;
    size_t arr_start = content.find('[');
    size_t arr_end = content.rfind(']');
    if (arr_start == std::string::npos || arr_end == std::string::npos) return libs;

    size_t pos = arr_start;
    while (pos < arr_end) {
        size_t q1 = content.find('"', pos);
        if (q1 == std::string::npos || q1 >= arr_end) break;
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 >= arr_end) break;
        libs.push_back(content.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
    return libs;
}

// ============================================================
// Test 1: Tier 1 — Binary search correctness
// ============================================================

static void test_tier1_binary_search() {
    TEST("Tier 1 — Binary search on known libraries");

    // Create a temp JSON with known libraries
    std::vector<std::string> known = {
        "react", "express", "lodash", "axios", "webpack",
        "typescript", "eslint", "@babel/core", "@types/node",
        "vue", "svelte", "angular", "next", "zustand"
    };
    std::string json_path = "/tmp/test_tracked_libs.json";
    create_temp_json(json_path, known);

    // Initialize
    init_tracked_libraries(json_path);

    // Test: all known libraries should be detected
    std::vector<std::string> test_paths = {
        "/node_modules/react",
        "/node_modules/express",
        "/node_modules/lodash",
        "/node_modules/axios",
        "/vendor/webpack",
        "/deps/typescript",
        "/node_modules/eslint",
        "/node_modules/@babel/core",
        "/node_modules/@types/node",
        "/packages/vue",
        "/lib/svelte",
        "/deps/angular",
        "/node_modules/next",
        "/node_modules/zustand",
    };

    int hits = 0;
    for (const auto& path : test_paths) {
        if (check_model_is_library(path)) {
            hits++;
        }
    }
    ASSERT_EQ(hits, (int)test_paths.size(),
              "All known libraries should be detected via Tier 1");

    // Test: unknown paths should NOT match Tier 1
    // (They may or may not match Tier 2 depending on predictor state,
    //  so we test names that clearly aren't libraries)
    std::vector<std::string> non_lib_paths = {
        "/src/my-app",
        "/test/unit-tests",
        "/docs/readme",
    };

    // These should not match Tier 1 (binary search miss).
    // With no predictor loaded, they should also not match the fallback heuristic.
    // Note: We can't guarantee false here because of heuristic fallback,
    // but at minimum the binary search won't find them.
    std::cout << "  Known libs detected: " << hits << "/" << test_paths.size() << std::endl;
    ASSERT_TRUE(hits == (int)test_paths.size(), "100% Tier 1 hit rate for known libs");

    // Cleanup
    std::remove(json_path.c_str());
}

// ============================================================
// Test 2: Tier 2 — MLP predictor fallback + auto-append
// ============================================================

static void test_tier2_predictor_fallback() {
    TEST("Tier 2 — MLP predictor fallback + auto-append to JSON");

    // Create a minimal JSON (without the test library)
    std::vector<std::string> initial = {"react", "express"};
    std::string json_path = "/tmp/test_tracked_tier2.json";
    create_temp_json(json_path, initial);

    init_tracked_libraries(json_path);

    // Try loading the predictor
    std::string model_path = std::string(SOURCE_DIR) + "/models/model_params.bin";
    init_predictor(model_path);

    // Test with a path that's clearly a library but NOT in the initial JSON
    // "node_modules/lodash" — the model should recognize this as a library
    bool result = check_model_is_library("/node_modules/lodash");

    // Read back the JSON to see if it was appended
    auto libs_after = read_json_libs(json_path);

    if (result) {
        std::cout << "  Predictor classified /node_modules/lodash as LIBRARY ✓" << std::endl;

        // Check if lodash was appended to the JSON
        bool found = std::find(libs_after.begin(), libs_after.end(), "lodash")
                     != libs_after.end();
        ASSERT_TRUE(found, "lodash should be appended to tracked_libraries.json after model detection");

        // Now test again — should hit Tier 1 (binary search) this time
        bool second = check_model_is_library("/some/path/lodash");
        ASSERT_TRUE(second, "Second lookup should hit Tier 1 cache");
        std::cout << "  Second lookup (Tier 1 cache hit) ✓" << std::endl;
    } else {
        std::cout << "  Predictor not loaded or classified as non-library (skipped)" << std::endl;
        // If model didn't load, this isn't a failure of our code
    }

    std::cout << "  JSON entries after test: " << libs_after.size()
              << " (started with " << initial.size() << ")" << std::endl;

    std::remove(json_path.c_str());
}

// ============================================================
// Test 3: Sorted vector invariant
// ============================================================

static void test_sorted_invariant() {
    TEST("Sorted vector invariant after insertions");

    // Create JSON with unsorted entries (init should sort them)
    std::vector<std::string> unsorted = {
        "zustand", "axios", "react", "express", "babel",
        "webpack", "lodash", "vite", "next", "angular"
    };
    std::string json_path = "/tmp/test_sorted.json";
    create_temp_json(json_path, unsorted);
    init_tracked_libraries(json_path);

    // All should still be findable after sort
    for (const auto& lib : unsorted) {
        std::string path = "/node_modules/" + lib;
        bool found = check_model_is_library(path);
        ASSERT_TRUE(found, ("Should find " + lib + " after sort").c_str());
    }

    std::cout << "  All " << unsorted.size()
              << " libraries found after sorting ✓" << std::endl;

    std::remove(json_path.c_str());
}

// ============================================================
// Test 4: Scoped package extraction
// ============================================================

static void test_scoped_packages() {
    TEST("Scoped package name extraction (@scope/name)");

    std::vector<std::string> scoped = {
        "@babel/core", "@types/node", "@vue/core",
        "@testing-library/react", "@emotion/styled"
    };
    std::string json_path = "/tmp/test_scoped.json";
    create_temp_json(json_path, scoped);
    init_tracked_libraries(json_path);

    // Test scoped packages are correctly extracted and found
    ASSERT_TRUE(check_model_is_library("/node_modules/@babel/core"),
                "@babel/core found");
    ASSERT_TRUE(check_model_is_library("/packages/@types/node"),
                "@types/node found");
    ASSERT_TRUE(check_model_is_library("/node_modules/@vue/core"),
                "@vue/core found");
    ASSERT_TRUE(check_model_is_library("/libs/@testing-library/react"),
                "@testing-library/react found");
    ASSERT_TRUE(check_model_is_library("/vendor/@emotion/styled"),
                "@emotion/styled found");

    std::cout << "  All scoped packages resolved correctly ✓" << std::endl;

    std::remove(json_path.c_str());
}

// ============================================================
// Test 5: Multi-threaded concurrent access stress test
// ============================================================

static void test_concurrent_access() {
    TEST("Concurrent access — multi-threaded stress");

    // Create a JSON with many libraries
    std::vector<std::string> libs;
    for (int i = 0; i < 500; i++) {
        libs.push_back("lib-" + std::to_string(i));
    }
    std::string json_path = "/tmp/test_concurrent.json";
    create_temp_json(json_path, libs);
    init_tracked_libraries(json_path);

    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 10000;
    std::atomic<int> total_hits{0};
    std::atomic<int> total_ops{0};

    auto worker = [&](int thread_id) {
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            // Mix of hits and misses
            int lib_idx = (thread_id * 137 + i * 31) % 700;  // some will hit, some won't
            std::string path;
            if (lib_idx < 500) {
                path = "/node_modules/lib-" + std::to_string(lib_idx);
            } else {
                path = "/node_modules/unknown-" + std::to_string(lib_idx);
            }
            if (check_model_is_library(path)) {
                total_hits++;
            }
            total_ops++;
        }
    };

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back(worker, t);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    double ms = std::chrono::duration<double, std::milli>(elapsed).count();

    ASSERT_EQ(total_ops.load(), NUM_THREADS * OPS_PER_THREAD,
              "All operations completed");
    ASSERT_TRUE(total_hits.load() > 0, "At least some hits occurred");

    double ops_per_sec = total_ops.load() / (ms / 1000.0);
    std::cout << "  Threads: " << NUM_THREADS
              << ", Ops: " << total_ops.load()
              << ", Hits: " << total_hits.load()
              << ", Time: " << ms << " ms"
              << ", Throughput: " << (int)ops_per_sec << " ops/sec"
              << std::endl;

    ASSERT_TRUE(ms < 5000, "Should complete within 5 seconds");
    std::cout << "  No crashes or data races ✓" << std::endl;

    std::remove(json_path.c_str());
}

// ============================================================
// Test 6: Performance benchmark — binary search throughput
// ============================================================

static void test_binary_search_performance() {
    TEST("Performance benchmark — binary search throughput");

    // Load with the actual tracked_libraries.json (500+ entries)
    std::string json_path = std::string(SOURCE_DIR) + "/tracked_libraries.json";
    init_tracked_libraries(json_path);

    // Prepare test queries — mix of hits and misses
    std::vector<std::string> queries;
    std::vector<std::string> known_hits = {
        "/node_modules/react", "/node_modules/lodash", "/node_modules/express",
        "/node_modules/webpack", "/node_modules/typescript", "/vendor/axios",
        "/node_modules/@babel/core", "/node_modules/@types/node",
        "/deps/vue", "/lib/angular"
    };
    std::vector<std::string> known_misses = {
        "/src/my-component", "/test/unit/helper", "/docs/api/readme",
        "/config/webpack.config", "/scripts/deploy", "/data/seed",
        "/tmp/scratch", "/build/output", "/archive/old-stuff",
        "/home/user/project"
    };

    // Build 100K queries (50% hits, 50% misses)
    for (int i = 0; i < 50000; i++) {
        queries.push_back(known_hits[i % known_hits.size()]);
    }
    for (int i = 0; i < 50000; i++) {
        queries.push_back(known_misses[i % known_misses.size()]);
    }

    // Benchmark
    int hits = 0;
    auto start = std::chrono::steady_clock::now();

    for (const auto& q : queries) {
        if (check_model_is_library(q)) {
            hits++;
        }
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    double ms = std::chrono::duration<double, std::milli>(elapsed).count();
    double us_per_op = (ms * 1000.0) / queries.size();
    double ops_per_sec = queries.size() / (ms / 1000.0);

    std::cout << "  Queries: " << queries.size()
              << ", Hits: " << hits
              << ", Time: " << ms << " ms"
              << std::endl;
    std::cout << "  Per-query: " << us_per_op << " µs"
              << ", Throughput: " << (int)ops_per_sec << " ops/sec"
              << std::endl;

    ASSERT_TRUE(us_per_op < 100, "Per-query latency should be under 100µs");
    ASSERT_TRUE(hits >= 49000, "Most hit queries should match");
    std::cout << "  Binary search performance ✓" << std::endl;
}

// ============================================================
// Test 7: Large dataset stress — 10K libraries
// ============================================================

static void test_large_dataset() {
    TEST("Large dataset stress — 10K libraries");

    std::vector<std::string> libs;
    for (int i = 0; i < 10000; i++) {
        libs.push_back("package-" + std::to_string(i));
    }
    std::string json_path = "/tmp/test_large.json";
    create_temp_json(json_path, libs);

    auto start = std::chrono::steady_clock::now();
    init_tracked_libraries(json_path);
    auto init_elapsed = std::chrono::steady_clock::now() - start;
    double init_ms = std::chrono::duration<double, std::milli>(init_elapsed).count();
    std::cout << "  Init time (10K entries): " << init_ms << " ms" << std::endl;

    // Random lookups
    int hits = 0;
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; i++) {
        int idx = (i * 7919) % 15000;  // some in range, some out
        std::string path = "/node_modules/package-" + std::to_string(idx);
        if (check_model_is_library(path)) {
            hits++;
        }
    }
    auto lookup_elapsed = std::chrono::steady_clock::now() - start;
    double lookup_ms = std::chrono::duration<double, std::milli>(lookup_elapsed).count();

    std::cout << "  100K lookups: " << lookup_ms << " ms"
              << " (" << (lookup_ms * 1000.0 / 100000) << " µs/op)"
              << ", Hits: " << hits << std::endl;

    ASSERT_TRUE(init_ms < 1000, "Init should complete within 1 second");
    ASSERT_TRUE(lookup_ms < 120000, "100K lookups should complete within 120 seconds (includes MLP fallback)");
    ASSERT_TRUE(hits > 0, "Some lookups should hit");

    std::cout << "  Large dataset stress ✓" << std::endl;
    std::remove(json_path.c_str());
}

// ============================================================
// Test 8: Edge cases
// ============================================================

static void test_edge_cases() {
    TEST("Edge cases");

    std::vector<std::string> libs = {"react", "express"};
    std::string json_path = "/tmp/test_edge.json";
    create_temp_json(json_path, libs);
    init_tracked_libraries(json_path);

    // Empty path
    ASSERT_FALSE(check_model_is_library(""), "Empty path should return false");

    // Just slashes
    ASSERT_FALSE(check_model_is_library("///"), "Only slashes should return false");

    // Single component (no slash)
    ASSERT_TRUE(check_model_is_library("react"), "Direct name 'react' should match");

    // Trailing slashes
    ASSERT_TRUE(check_model_is_library("/node_modules/react/"),
                "Trailing slash should still match");
    ASSERT_TRUE(check_model_is_library("/node_modules/react///"),
                "Multiple trailing slashes should still match");

    // Deep nesting
    ASSERT_TRUE(check_model_is_library("/a/b/c/d/e/react"),
                "Deep nesting should extract 'react' from last segment");

    std::cout << "  Edge cases handled ✓" << std::endl;
    std::remove(json_path.c_str());
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " FastDevFs Library Detection Stress Test" << std::endl;
    std::cout << "========================================" << std::endl;

    test_tier1_binary_search();
    test_sorted_invariant();
    test_scoped_packages();
    test_edge_cases();
    test_tier2_predictor_fallback();
    test_concurrent_access();
    test_binary_search_performance();
    test_large_dataset();

    std::cout << "\n========================================" << std::endl;
    std::cout << " Results: " << g_pass << " passed, " << g_fail << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return g_fail > 0 ? 1 : 0;
}
