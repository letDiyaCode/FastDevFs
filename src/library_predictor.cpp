/**
 * @file library_predictor.cpp
 * @brief Implementation of the MLP-based library folder classifier.
 *
 * This file replicates the trained sklearn MLP-ReLU pipeline in pure C++17
 * with zero external dependencies. It implements:
 *   1. Path normalization (Windows/Unix)
 *   2. Tokenization and character n-gram extraction
 *   3. 42 hand-crafted features (structural, lexical, semantic)
 *   4. TF-IDF vectorization (token + char n-gram)
 *   5. StandardScaler normalization
 *   6. MLP forward pass with ReLU activation
 *   7. Sigmoid output for probability
 */

#include "library_predictor.h"

#include <fstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <regex>
#include <iostream>
#include <cstring>
#include <set>

namespace fastdevfs {

// ============================================================================
// STATIC KNOWN TOKEN SETS
// ============================================================================

const std::unordered_set<std::string> LibraryPredictor::LIB_TOKENS = {
    // Python ecosystem
    "site-packages", "dist-packages", "__pycache__", ".venv", "venv",
    "virtualenv", ".tox", ".nox", ".mypy_cache", ".pytest_cache",
    "egg-info", ".eggs", "pip", "setuptools", "wheel",
    // Node/JS ecosystem
    "node_modules", "bower_components", "jspm_packages",
    // General dependency dirs
    "vendor", "vendors", "lib", "libs", "library", "libraries",
    "third_party", "third-party", "thirdparty", "3rdparty",
    "external", "externals", "extern", "ext",
    "deps", "dependencies", "packages", "pkg",
    // Build / cache
    "target", "build", "dist", "out", ".gradle", ".maven", ".m2",
    // Ruby
    "gems", "bundle", "bundler",
    // Rust
    ".cargo", "cargo-registry",
    // Go
    "go-vendor", "pkg/mod",
    // iOS / macOS
    "Pods", "Carthage", "DerivedData",
    // .NET
    "nuget", ".nuget", "paket-files",
    // R
    "renv", "packrat",
    // General
    "modules", "addons", "plugins", "contrib",
    "sdk", "framework", "frameworks", "runtime",
};

const std::unordered_set<std::string> LibraryPredictor::NONLIB_TOKENS = {
    "src", "source", "app", "application", "main",
    "test", "tests", "testing", "spec", "specs", "e2e",
    "doc", "docs", "documentation", "wiki",
    "config", "configs", "configuration", "settings",
    "script", "scripts", "tools", "utils",
    "example", "examples", "sample", "samples", "demo",
    "asset", "assets", "resource", "resources", "static",
    "public", "www", "web", "htdocs",
    "data", "database", "db", "migrations", "seeds",
    "media", "images", "img", "icons", "fonts", "styles", "css",
    "template", "templates", "views", "layouts", "pages", "components",
    "model", "models", "controller", "controllers",
    "service", "services", "handler", "handlers",
    "middleware", "route", "routes", "api",
    "deploy", "deployment", "devops", "ci", "cd",
    "docker", "k8s", "terraform", "ansible",
    "logs", "log", "logging", "monitoring",
    "backup", "backups", "archive", "temp", "tmp",
    ".git", ".svn", ".github", ".gitlab",
    ".vscode", ".idea",
    "sprint", "release", "hotfix", "bugfix", "refactor",
    "feature", "cleanup", "draft", "wip",
};

const std::unordered_set<std::string> LibraryPredictor::KNOWN_SCOPES = {
    "types", "babel", "angular", "vue", "react", "emotion", "mui",
    "jest", "testing-library", "storybook", "typescript-eslint",
    "rollup", "webpack", "vitejs", "sveltejs", "nestjs", "nrwl",
    "aws-sdk", "google-cloud", "azure", "octokit", "sentry",
    "graphql-tools", "apollo", "prisma", "trpc", "tanstack",
    "reduxjs", "remix-run", "next", "vercel", "netlify",
    "radix-ui", "headlessui", "floating-ui", "dnd-kit",
    "fortawesome", "mantine", "chakra-ui", "ant-design",
    "opentelemetry", "grpc", "bufbuild", "connectrpc",
    "tensorflow", "huggingface", "langchain",
};


// ============================================================================
// BINARY FILE READING HELPERS
// ============================================================================

uint32_t LibraryPredictor::read_uint32(std::ifstream& f) {
    uint32_t val;
    f.read(reinterpret_cast<char*>(&val), sizeof(val));
    return val;
}

double LibraryPredictor::read_float64(std::ifstream& f) {
    double val;
    f.read(reinterpret_cast<char*>(&val), sizeof(val));
    return val;
}

std::vector<double> LibraryPredictor::read_float64_array(std::ifstream& f) {
    uint32_t count = read_uint32(f);
    std::vector<double> arr(count);
    f.read(reinterpret_cast<char*>(arr.data()), count * sizeof(double));
    return arr;
}

std::string LibraryPredictor::read_string(std::ifstream& f) {
    uint32_t len = read_uint32(f);
    std::string s(len, '\0');
    f.read(&s[0], len);
    return s;
}

std::vector<std::string> LibraryPredictor::read_string_list(std::ifstream& f) {
    uint32_t count = read_uint32(f);
    std::vector<std::string> list(count);
    for (uint32_t i = 0; i < count; ++i) {
        list[i] = read_string(f);
    }
    return list;
}


// ============================================================================
// MODEL LOADING
// ============================================================================

bool LibraryPredictor::load(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "LibraryPredictor: Cannot open " << filepath << std::endl;
        return false;
    }

    // Read and verify magic header
    char magic[8];
    f.read(magic, 8);
    if (std::strncmp(magic, "MLPV1", 5) != 0) {
        std::cerr << "LibraryPredictor: Invalid file format" << std::endl;
        return false;
    }

    // Section 1: MLP layers
    uint32_t num_layers = read_uint32(f);
    layers_.resize(num_layers);
    for (uint32_t i = 0; i < num_layers; ++i) {
        layers_[i].input_dim = read_uint32(f);
        layers_[i].output_dim = read_uint32(f);
        layers_[i].weights = read_float64_array(f);
        layers_[i].biases = read_float64_array(f);
    }

    // Section 2: Token TF-IDF vocabulary + IDF
    auto token_terms = read_string_list(f);
    token_idf_ = read_float64_array(f);
    token_vocab_.clear();
    for (uint32_t i = 0; i < token_terms.size(); ++i) {
        token_vocab_[token_terms[i]] = i;
    }

    // Section 3: Char n-gram TF-IDF vocabulary + IDF
    auto char_terms = read_string_list(f);
    char_idf_ = read_float64_array(f);
    char_vocab_.clear();
    for (uint32_t i = 0; i < char_terms.size(); ++i) {
        char_vocab_[char_terms[i]] = i;
    }

    // Section 4: Scaler parameters
    scaler_mean_ = read_float64_array(f);
    scaler_scale_ = read_float64_array(f);

    // Section 5: Feature names
    feature_names_ = read_string_list(f);

    loaded_ = true;
    return true;
}


// ============================================================================
// PATH NORMALIZATION AND TOKENIZATION
// ============================================================================

std::string LibraryPredictor::normalize_path(const std::string& path) {
    std::string result;
    result.reserve(path.size());

    // Replace backslashes with forward slashes
    for (char c : path) {
        if (c == '\\') {
            result += '/';
        } else {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    // Strip leading/trailing slashes and whitespace
    size_t start = result.find_first_not_of(" /");
    size_t end = result.find_last_not_of(" /");
    if (start == std::string::npos) return "";
    result = result.substr(start, end - start + 1);

    // Collapse multiple slashes
    std::string collapsed;
    collapsed.reserve(result.size());
    bool prev_slash = false;
    for (char c : result) {
        if (c == '/') {
            if (!prev_slash) {
                collapsed += c;
            }
            prev_slash = true;
        } else {
            collapsed += c;
            prev_slash = false;
        }
    }

    return collapsed;
}

std::vector<std::string> LibraryPredictor::tokenize_path(const std::string& path) {
    std::string normalized = normalize_path(path);
    std::vector<std::string> tokens;

    std::string current;
    for (char c : normalized) {
        if (c == '/' || c == '-' || c == '_' || c == '.' || c == '@' || c == ' ') {
            if (!current.empty()) {
                // Skip pure numeric tokens
                bool all_digits = true;
                for (char ch : current) {
                    if (!std::isdigit(static_cast<unsigned char>(ch))) {
                        all_digits = false;
                        break;
                    }
                }
                if (!all_digits) {
                    tokens.push_back(current);
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        bool all_digits = true;
        for (char ch : current) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                all_digits = false;
                break;
            }
        }
        if (!all_digits) {
            tokens.push_back(current);
        }
    }

    return tokens;
}

std::vector<std::string> LibraryPredictor::extract_char_ngrams(const std::string& path, int n) {
    std::string normalized = normalize_path(path);
    std::vector<std::string> ngrams;
    if (static_cast<int>(normalized.size()) < n) return ngrams;

    ngrams.reserve(normalized.size() - n + 1);
    for (size_t i = 0; i <= normalized.size() - n; ++i) {
        ngrams.push_back(normalized.substr(i, n));
    }
    return ngrams;
}


// ============================================================================
// HAND-CRAFTED FEATURE EXTRACTION
// ============================================================================

// Helper: check if string ends with a suffix
static bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Helper: check if string starts with a prefix
static bool starts_with(const std::string& str, const std::string& prefix) {
    if (prefix.size() > str.size()) return false;
    return str.compare(0, prefix.size(), prefix) == 0;
}

// Helper: split string by a delimiter character
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::string part;
    for (char c : s) {
        if (c == delim) {
            parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    parts.push_back(part);
    return parts;
}

// Helper: simple regex-like pattern matching
static bool has_version_pattern(const std::string& s) {
    // Match: @v?N.N or N.N.N
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        // Pattern: digit.digit
        if (std::isdigit(static_cast<unsigned char>(s[i])) && s[i + 1] == '.' &&
            std::isdigit(static_cast<unsigned char>(s[i + 2]))) {
            // Check for @v prefix or -.digit prefix
            if (i >= 1 && (s[i - 1] == '@' || s[i - 1] == '-' || s[i - 1] == 'v')) {
                return true;
            }
            // Check for N.N.N pattern
            if (i + 4 < s.size() && s[i + 3] == '.' &&
                std::isdigit(static_cast<unsigned char>(s[i + 4]))) {
                return true;
            }
        }
    }
    return false;
}

static bool ends_with_digits(const std::string& s) {
    if (s.empty()) return false;
    return std::isdigit(static_cast<unsigned char>(s.back()));
}

static bool has_numbered_suffix(const std::string& s) {
    // Pattern: [-_] followed by 1-4 digits at end
    if (s.size() < 2) return false;
    size_t i = s.size() - 1;
    int digit_count = 0;
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i]))) {
        digit_count++;
        i--;
    }
    if (digit_count >= 1 && digit_count <= 4 && (s[i] == '-' || s[i] == '_')) {
        return true;
    }
    return false;
}

static bool has_date_pattern(const std::string& s) {
    static const std::vector<std::string> months = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec"
    };
    for (const auto& m : months) {
        size_t pos = s.find(m);
        if (pos != std::string::npos) {
            // Check if followed by optional [-_] then 4 digits
            size_t next = pos + m.size();
            if (next < s.size() && (s[next] == '-' || s[next] == '_')) {
                next++;
            }
            if (next + 3 < s.size()) {
                bool all_digits = true;
                for (int j = 0; j < 4; ++j) {
                    if (!std::isdigit(static_cast<unsigned char>(s[next + j]))) {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits) return true;
            }
        }
    }
    return false;
}

std::vector<double> LibraryPredictor::extract_handcrafted_features(const std::string& path) const {
    std::string normalized = normalize_path(path);
    std::string original = path;

    // Trim whitespace from original
    size_t s = original.find_first_not_of(" \t\r\n");
    size_t e = original.find_last_not_of(" \t\r\n");
    if (s != std::string::npos) original = original.substr(s, e - s + 1);

    auto tokens = tokenize_path(path);
    auto segments = split(normalized, '/');
    std::string last_segment = segments.empty() ? normalized : segments.back();

    // Build sets for fast lookup
    std::unordered_set<std::string> token_set(tokens.begin(), tokens.end());
    std::unordered_set<std::string> segment_set(segments.begin(), segments.end());

    // Feature vector (42 features, must match Python order exactly)
    std::vector<double> features;
    features.reserve(42);

    // --- Structural Features ---
    // path_length
    features.push_back(static_cast<double>(normalized.size()));
    // path_depth
    features.push_back(static_cast<double>(std::count(normalized.begin(), normalized.end(), '/')));
    // num_segments
    features.push_back(static_cast<double>(segments.size()));
    // last_segment_length
    features.push_back(static_cast<double>(last_segment.size()));
    // avg_segment_length
    {
        double avg = 0;
        if (!segments.empty()) {
            double sum = 0;
            for (const auto& seg : segments) sum += seg.size();
            avg = sum / segments.size();
        }
        features.push_back(avg);
    }
    // num_tokens
    features.push_back(static_cast<double>(tokens.size()));

    // --- Character-level Features ---
    int num_hyphens = static_cast<int>(std::count(normalized.begin(), normalized.end(), '-'));
    int num_underscores = static_cast<int>(std::count(normalized.begin(), normalized.end(), '_'));
    int num_dots = static_cast<int>(std::count(normalized.begin(), normalized.end(), '.'));
    int num_slashes = static_cast<int>(std::count(normalized.begin(), normalized.end(), '/'));
    int num_digits = 0;
    for (char c : normalized) {
        if (std::isdigit(static_cast<unsigned char>(c))) num_digits++;
    }
    int num_uppercase = 0;
    for (char c : original) {
        if (std::isupper(static_cast<unsigned char>(c))) num_uppercase++;
    }

    features.push_back(static_cast<double>(num_hyphens));
    features.push_back(static_cast<double>(num_underscores));
    features.push_back(static_cast<double>(num_dots));
    features.push_back(static_cast<double>(num_slashes));
    features.push_back(static_cast<double>(num_digits));
    features.push_back(static_cast<double>(num_uppercase));

    double path_len = std::max(static_cast<double>(normalized.size()), 1.0);
    features.push_back(num_digits / path_len);      // digit_ratio
    features.push_back(num_hyphens / path_len);      // hyphen_ratio

    // --- Pattern-based Binary Features ---
    features.push_back(normalized.empty() ? 0.0 : (normalized[0] == '.' ? 1.0 : 0.0));  // starts_with_dot
    features.push_back(normalized.empty() ? 0.0 : (normalized[0] == '@' ? 1.0 : 0.0));  // starts_with_at
    features.push_back(normalized.find('@') != std::string::npos ? 1.0 : 0.0);           // has_at_sign
    features.push_back(normalized.find('/') != std::string::npos ? 1.0 : 0.0);           // has_slash

    // is_scoped_npm
    features.push_back((!normalized.empty() && normalized[0] == '@' &&
                         normalized.find('/') != std::string::npos) ? 1.0 : 0.0);

    // has_version_pattern
    features.push_back(has_version_pattern(normalized) ? 1.0 : 0.0);

    // ends_with_digits
    features.push_back(ends_with_digits(normalized) ? 1.0 : 0.0);

    // has_double_underscore
    features.push_back(normalized.find("__") != std::string::npos ? 1.0 : 0.0);

    // --- Semantic Token Features ---
    int lib_token_hits = 0;
    for (const auto& t : token_set) {
        if (LIB_TOKENS.count(t)) lib_token_hits++;
    }
    int nonlib_token_hits = 0;
    for (const auto& t : token_set) {
        if (NONLIB_TOKENS.count(t)) nonlib_token_hits++;
    }

    features.push_back(static_cast<double>(lib_token_hits));
    features.push_back(static_cast<double>(nonlib_token_hits));
    double num_tok = std::max(static_cast<double>(tokens.size()), 1.0);
    features.push_back(lib_token_hits / num_tok);      // lib_token_ratio
    features.push_back(nonlib_token_hits / num_tok);    // nonlib_token_ratio

    // has_lib_segment
    {
        bool found = false;
        for (const auto& seg : segment_set) {
            if (LIB_TOKENS.count(seg)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }
    // has_nonlib_segment
    {
        bool found = false;
        for (const auto& seg : segment_set) {
            if (NONLIB_TOKENS.count(seg)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }

    // Specific high-signal patterns
    features.push_back(normalized.find("node_modules") != std::string::npos ? 1.0 : 0.0);
    features.push_back((normalized.find("site-packages") != std::string::npos ||
                         normalized.find("site_packages") != std::string::npos) ? 1.0 : 0.0);
    features.push_back(segment_set.count("vendor") ? 1.0 : 0.0);
    features.push_back((segment_set.count("packages") || segment_set.count("pkg")) ? 1.0 : 0.0);
    features.push_back((segment_set.count(".venv") || segment_set.count("venv") ||
                         segment_set.count("virtualenv")) ? 1.0 : 0.0);
    features.push_back(normalized.find("__pycache__") != std::string::npos ? 1.0 : 0.0);
    features.push_back((segment_set.count("lib") || segment_set.count("libs")) ? 1.0 : 0.0);

    // is_known_scope
    {
        double known_scope = 0.0;
        if (!normalized.empty() && normalized[0] == '@') {
            auto slash_pos = normalized.find('/');
            if (slash_pos != std::string::npos) {
                std::string scope_part = normalized.substr(1, slash_pos - 1);
                if (KNOWN_SCOPES.count(scope_part)) known_scope = 1.0;
            }
        }
        features.push_back(known_scope);
    }

    // has_lib_suffix
    {
        static const std::vector<std::string> lib_suffixes = {
            "lib", "sdk", "kit", "api", "io", "js", "py",
            "orm", "db", "cli", "ui", "ify"
        };
        bool found = false;
        for (const auto& s : lib_suffixes) {
            if (ends_with(last_segment, s)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }

    // has_lib_prefix
    {
        static const std::vector<std::string> lib_prefixes = {
            "py", "go-", "js-", "ts-", "lib", "fast", "easy",
            "simple", "tiny", "micro", "nano", "super", "hyper",
            "open", "node-", "react-", "vue-", "angular-"
        };
        bool found = false;
        for (const auto& p : lib_prefixes) {
            if (starts_with(last_segment, p)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }

    // has_nonlib_prefix
    {
        static const std::vector<std::string> nonlib_prefixes = {
            "my-", "my_", "our-", "our_", "the-", "the_",
            "test-", "test_", "dev-", "dev_"
        };
        bool found = false;
        for (const auto& p : nonlib_prefixes) {
            if (starts_with(last_segment, p)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }

    // has_numbered_suffix
    features.push_back(has_numbered_suffix(normalized) ? 1.0 : 0.0);

    // has_workflow_token
    {
        static const std::vector<std::string> workflow_patterns = {
            "sprint", "release", "hotfix", "bugfix",
            "refactor", "cleanup", "milestone"
        };
        bool found = false;
        for (const auto& wp : workflow_patterns) {
            if (token_set.count(wp)) { found = true; break; }
        }
        features.push_back(found ? 1.0 : 0.0);
    }

    // has_date_pattern
    features.push_back(has_date_pattern(normalized) ? 1.0 : 0.0);

    return features;  // Should be 42 features
}


// ============================================================================
// TF-IDF COMPUTATION
// ============================================================================

std::vector<double> LibraryPredictor::compute_tfidf(
    const std::vector<std::string>& terms,
    const std::unordered_map<std::string, uint32_t>& vocab,
    const std::vector<double>& idf,
    bool sublinear_tf
) const {
    std::vector<double> result(idf.size(), 0.0);

    // Count term frequencies
    std::unordered_map<uint32_t, int> tf_counts;
    for (const auto& term : terms) {
        auto it = vocab.find(term);
        if (it != vocab.end()) {
            tf_counts[it->second]++;
        }
    }

    // For token TF-IDF with bigrams, also count bigrams
    // The Python tokenizer produces unigrams from tokenize_path, then
    // TfidfVectorizer with ngram_range=(1,2) generates bigrams internally.
    // We replicate that here.
    if (&vocab == &token_vocab_) {
        // Generate bigrams from the terms list
        for (size_t i = 0; i + 1 < terms.size(); ++i) {
            std::string bigram = terms[i] + " " + terms[i + 1];
            auto it = vocab.find(bigram);
            if (it != vocab.end()) {
                tf_counts[it->second]++;
            }
        }
    }

    // Compute TF-IDF values
    for (const auto& [idx, count] : tf_counts) {
        double tf;
        if (sublinear_tf) {
            tf = 1.0 + std::log(static_cast<double>(count));
        } else {
            tf = static_cast<double>(count);
        }
        result[idx] = tf * idf[idx];
    }

    // L2 normalize (sklearn default)
    double norm = 0.0;
    for (double v : result) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0.0) {
        for (double& v : result) v /= norm;
    }

    return result;
}


// ============================================================================
// FEATURE VECTOR CONSTRUCTION
// ============================================================================

std::vector<double> LibraryPredictor::build_features(const std::string& path) const {
    // 1. Hand-crafted features (42)
    auto handcrafted = extract_handcrafted_features(path);

    // StandardScaler: (x - mean) / scale
    for (size_t i = 0; i < handcrafted.size() && i < scaler_mean_.size(); ++i) {
        handcrafted[i] = (handcrafted[i] - scaler_mean_[i]) / scaler_scale_[i];
    }

    // 2. Token TF-IDF (3000)
    auto path_tokens = tokenize_path(path);
    auto token_tfidf = compute_tfidf(path_tokens, token_vocab_, token_idf_, true);

    // 3. Char n-gram TF-IDF (2000)
    auto char_ngrams = extract_char_ngrams(path, 3);
    auto char_tfidf = compute_tfidf(char_ngrams, char_vocab_, char_idf_, true);

    // 4. Concatenate: [handcrafted(42) | token_tfidf(3000) | char_tfidf(2000)] = 5042
    std::vector<double> features;
    features.reserve(handcrafted.size() + token_tfidf.size() + char_tfidf.size());
    features.insert(features.end(), handcrafted.begin(), handcrafted.end());
    features.insert(features.end(), token_tfidf.begin(), token_tfidf.end());
    features.insert(features.end(), char_tfidf.begin(), char_tfidf.end());

    return features;
}


// ============================================================================
// MLP FORWARD PASS
// ============================================================================

double LibraryPredictor::forward(const std::vector<double>& features) const {
    // Layer 0 is the bottleneck (5042 → 512). Since the input is very sparse
    // (most TF-IDF features are zero), we skip zero entries entirely.
    // This gives ~10-20x speedup on the first layer.

    std::vector<double> current;

    // --- First layer: sparse input optimization ---
    {
        const auto& layer = layers_[0];
        current.resize(layer.output_dim);

        // Start with biases
        for (uint32_t j = 0; j < layer.output_dim; ++j) {
            current[j] = layer.biases[j];
        }

        // Only accumulate for non-zero input features
        for (uint32_t i = 0; i < layer.input_dim; ++i) {
            double val = features[i];
            if (val == 0.0) continue;  // skip zeros (vast majority)

            const double* w_row = &layer.weights[i * layer.output_dim];
            for (uint32_t j = 0; j < layer.output_dim; ++j) {
                current[j] += val * w_row[j];
            }
        }

        // ReLU activation
        for (double& v : current) {
            v = std::max(0.0, v);
        }
    }

    // --- Remaining layers: dense (all values are typically non-zero after ReLU) ---
    for (size_t layer_idx = 1; layer_idx < layers_.size(); ++layer_idx) {
        const auto& layer = layers_[layer_idx];
        std::vector<double> output(layer.output_dim, 0.0);

        // Dense matmul: output = current * W + b
        for (uint32_t j = 0; j < layer.output_dim; ++j) {
            double sum = layer.biases[j];
            for (uint32_t i = 0; i < layer.input_dim; ++i) {
                sum += current[i] * layer.weights[i * layer.output_dim + j];
            }
            output[j] = sum;
        }

        // Activation
        if (layer_idx < layers_.size() - 1) {
            // Hidden layers: ReLU
            for (double& v : output) {
                v = std::max(0.0, v);
            }
        } else {
            // Output layer: sigmoid (sklearn MLPClassifier with single output)
            for (double& v : output) {
                v = 1.0 / (1.0 + std::exp(-v));
            }
        }

        current = std::move(output);
    }

    // Single output neuron → probability of class 1 (library)
    return current[0];
}


// ============================================================================
// PUBLIC PREDICTION API
// ============================================================================

PredictionResult LibraryPredictor::predict(const std::string& path, double threshold) const {
    double prob = predict_proba(path);

    PredictionResult result;
    result.is_library = (prob >= threshold);
    result.probability = prob;

    double dist = std::abs(prob - 0.5);
    if (dist > 0.3) {
        result.confidence = "high";
    } else if (dist > 0.15) {
        result.confidence = "medium";
    } else {
        result.confidence = "low";
    }

    return result;
}

double LibraryPredictor::predict_proba(const std::string& path) const {
    if (!loaded_) return 0.0;
    auto features = build_features(path);
    return forward(features);
}

bool LibraryPredictor::is_library(const std::string& path, double threshold) const {
    return predict_proba(path) >= threshold;
}

} // namespace fastdevfs
