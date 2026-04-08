#pragma once
/**
 * @file library_predictor.h
 * @brief MLP-based library folder classifier for FastDevFs
 *
 * Predicts whether a folder path belongs to a library/dependency directory.
 * Replicates the trained sklearn MLP-ReLU model with:
 *   - 42 hand-crafted path features
 *   - Token TF-IDF (3000 features)
 *   - Character 3-gram TF-IDF (2000 features)
 *   - 4-layer MLP with ReLU activation (512→256→128→64→1)
 *
 * Usage:
 *   LibraryPredictor predictor;
 *   predictor.load("model_artifacts/model_params.bin");
 *   auto result = predictor.predict("node_modules/express");
 *   // result.is_library = true, result.probability = 0.9999
 */

#ifndef LIBRARY_PREDICTOR_H
#define LIBRARY_PREDICTOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace fastdevfs {

/**
 * @brief Result of a library folder prediction.
 */
struct PredictionResult {
    bool is_library;        ///< true if predicted as library
    double probability;     ///< probability of being a library [0, 1]
    std::string confidence; ///< "high", "medium", or "low"
};

/**
 * @brief Dense layer parameters for the MLP.
 */
struct DenseLayer {
    uint32_t input_dim;
    uint32_t output_dim;
    std::vector<double> weights;  // row-major: [input_dim x output_dim]
    std::vector<double> biases;   // [output_dim]
};

/**
 * @brief MLP-based library folder classifier.
 *
 * Loads a pre-trained model from a binary file and performs inference
 * on pathname strings. Zero external dependencies beyond the C++ stdlib.
 */
class LibraryPredictor {
public:
    LibraryPredictor() = default;
    ~LibraryPredictor() = default;

    // Non-copyable, movable
    LibraryPredictor(const LibraryPredictor&) = delete;
    LibraryPredictor& operator=(const LibraryPredictor&) = delete;
    LibraryPredictor(LibraryPredictor&&) = default;
    LibraryPredictor& operator=(LibraryPredictor&&) = default;

    /**
     * @brief Load model parameters from a binary file.
     * @param filepath Path to model_params.bin
     * @return true if loaded successfully
     */
    bool load(const std::string& filepath);

    /**
     * @brief Check if the model is loaded and ready.
     */
    bool is_loaded() const { return loaded_; }

    /**
     * @brief Predict whether a path is a library folder.
     * @param path The pathname string to classify
     * @param threshold Decision threshold (default 0.5)
     * @return PredictionResult with label, probability, and confidence
     */
    PredictionResult predict(const std::string& path, double threshold = 0.5) const;

    /**
     * @brief Get the raw probability (useful for custom thresholding).
     * @param path The pathname string
     * @return Probability of being a library [0, 1]
     */
    double predict_proba(const std::string& path) const;

    /**
     * @brief Quick boolean check (most common use case).
     * @param path The pathname string
     * @param threshold Decision threshold
     * @return true if the path is predicted as a library folder
     */
    bool is_library(const std::string& path, double threshold = 0.5) const;

private:
    // ---- Model parameters ----
    std::vector<DenseLayer> layers_;

    // ---- TF-IDF parameters ----
    std::unordered_map<std::string, uint32_t> token_vocab_;
    std::vector<double> token_idf_;

    std::unordered_map<std::string, uint32_t> char_vocab_;
    std::vector<double> char_idf_;

    // ---- Scaler parameters ----
    std::vector<double> scaler_mean_;
    std::vector<double> scaler_scale_;

    // ---- Feature names ----
    std::vector<std::string> feature_names_;

    bool loaded_ = false;

    // ---- Known token sets for hand-crafted features ----
    static const std::unordered_set<std::string> LIB_TOKENS;
    static const std::unordered_set<std::string> NONLIB_TOKENS;
    static const std::unordered_set<std::string> KNOWN_SCOPES;

    // ---- Internal methods ----

    /// Normalize path for cross-platform consistency
    static std::string normalize_path(const std::string& path);

    /// Tokenize a normalized path into words
    static std::vector<std::string> tokenize_path(const std::string& path);

    /// Extract character 3-grams as a vector of strings
    static std::vector<std::string> extract_char_ngrams(const std::string& path, int n = 3);

    /// Extract 42 hand-crafted features
    std::vector<double> extract_handcrafted_features(const std::string& path) const;

    /// Compute TF-IDF features for a single path
    std::vector<double> compute_tfidf(
        const std::vector<std::string>& terms,
        const std::unordered_map<std::string, uint32_t>& vocab,
        const std::vector<double>& idf,
        bool sublinear_tf = true
    ) const;

    /// Build the full feature vector (42 + 3000 + 2000 = 5042)
    std::vector<double> build_features(const std::string& path) const;

    /// MLP forward pass
    double forward(const std::vector<double>& features) const;

    // ---- Binary file reading helpers ----
    static uint32_t read_uint32(std::ifstream& f);
    static double read_float64(std::ifstream& f);
    static std::vector<double> read_float64_array(std::ifstream& f);
    static std::string read_string(std::ifstream& f);
    static std::vector<std::string> read_string_list(std::ifstream& f);
};

} // namespace fastdevfs

#endif // LIBRARY_PREDICTOR_H
