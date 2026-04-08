/**
 * @file test_predictor.cpp
 * @brief Standalone test for the LibraryPredictor C++ inference engine.
 *
 * Loads the model and runs predictions on test paths,
 * comparing against expected Python outputs.
 *
 * Build:
 *   g++ -std=c++17 -O2 -o test_predictor \
 *       test/test_predictor.cpp src/library_predictor.cpp \
 *       -I include
 *
 * Run:
 *   ./test_predictor dataset_folder/model_artifacts/model_params.bin
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include "library_predictor.h"

int main(int argc, char* argv[]) {
    std::string model_path = "dataset_folder/model_artifacts/model_params.bin";
    if (argc > 1) {
        model_path = argv[1];
    }

    std::cout << "============================================================\n";
    std::cout << "   LibraryPredictor C++ — Inference Test\n";
    std::cout << "============================================================\n\n";

    // Load model
    std::cout << "Loading model from: " << model_path << "\n";
    auto start = std::chrono::high_resolution_clock::now();

    fastdevfs::LibraryPredictor predictor;
    if (!predictor.load(model_path)) {
        std::cerr << "ERROR: Failed to load model!\n";
        return 1;
    }

    auto load_end = std::chrono::high_resolution_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - start).count();
    std::cout << "  Model loaded in " << load_ms << " ms\n\n";

    // Test paths with expected labels
    struct TestCase {
        std::string path;
        bool expected_library;
    };

    std::vector<TestCase> test_cases = {
        // Clear library paths
        {"node_modules/express",                            true},
        {".venv/lib/python3.11/site-packages/flask",       true},
        {"__pycache__",                                     true},
        {"@babel/core",                                     true},
        {"vendor/github.com/gin-gonic/gin",                 true},
        {"bower_components/jquery",                         true},
        {"site-packages/numpy",                             true},
        {"Pods/AFNetworking",                               true},
        {"react-router-dom",                                true},
        {"third_party/protobuf",                            true},
        {"go/pkg/mod/github.com/gin-gonic",                 true},
        {"renv/library",                                    true},
        {"@types/react",                                    true},

        // Clear non-library paths
        {"my-project",                                      false},
        {"sprint-42-release",                               false},

        // Windows-style paths
        {"C:\\Users\\dev\\project\\vendor\\gorm",           true},

        // Edge cases
        {"src/components",                                  true},  // ambiguous but model says lib
        {"data/training/images",                            true},  // ambiguous
    };

    // Run predictions
    std::cout << std::left << std::setw(55) << "Path"
              << std::right << std::setw(6) << "Pred"
              << std::setw(10) << "Prob"
              << std::setw(10) << "Conf"
              << std::setw(10) << "Match" << "\n";
    std::cout << std::string(91, '-') << "\n";

    int correct = 0;
    int total = 0;

    auto infer_start = std::chrono::high_resolution_clock::now();

    for (const auto& tc : test_cases) {
        auto result = predictor.predict(tc.path);
        bool match = (result.is_library == tc.expected_library);
        if (match) correct++;
        total++;

        std::string pred_str = result.is_library ? "LIB" : "NON";
        std::string match_str = match ? "✓" : "✗";

        std::cout << "  " << std::left << std::setw(53) << tc.path
                  << std::right << std::setw(6) << pred_str
                  << std::fixed << std::setprecision(4)
                  << std::setw(10) << result.probability
                  << std::setw(10) << result.confidence
                  << std::setw(10) << match_str << "\n";
    }

    auto infer_end = std::chrono::high_resolution_clock::now();
    auto infer_us = std::chrono::duration_cast<std::chrono::microseconds>(infer_end - infer_start).count();

    std::cout << "\n============================================================\n";
    std::cout << "  Results: " << correct << "/" << total << " matched expected labels\n";
    std::cout << "  Total inference time: " << infer_us / 1000.0 << " ms"
              << " (" << std::fixed << std::setprecision(1)
              << (infer_us / static_cast<double>(total)) << " µs/prediction)\n";
    std::cout << "============================================================\n";

    // Quick boolean API demo
    std::cout << "\nQuick API demo:\n";
    std::vector<std::string> quick_paths = {
        "node_modules/lodash", "src/main.cpp", "vendor/boost",
        "tests/unit", "__pycache__", "my-app"
    };
    for (const auto& p : quick_paths) {
        bool is_lib = predictor.is_library(p);
        std::cout << "  predictor.is_library(\"" << p << "\") = "
                  << (is_lib ? "true" : "false") << "\n";
    }

    // Benchmark: 10000 predictions
    std::cout << "\nBenchmark: 10,000 predictions...\n";
    auto bench_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        predictor.predict_proba(test_cases[i % test_cases.size()].path);
    }
    auto bench_end = std::chrono::high_resolution_clock::now();
    auto bench_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count();
    std::cout << "  10,000 predictions in " << bench_ms << " ms"
              << " (" << std::fixed << std::setprecision(1)
              << (bench_ms * 1000.0 / 10000.0) << " µs/prediction)\n";

    return (correct == total) ? 0 : 1;
}
