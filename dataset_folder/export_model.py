#!/usr/bin/env python3
"""
Export trained MLP-ReLU model parameters to a binary format
that the C++ predictor can load efficiently.

Binary format (.bin):
  - All multi-byte values are little-endian
  - Strings are written as: uint32_t length, then chars (no null terminator)
  - Arrays are written as: uint32_t count, then float64 values

Sections (in order):
  1. MLP weights and biases (5 layers: 4 hidden + 1 output)
  2. TF-IDF token vocabulary + IDF values
  3. TF-IDF char n-gram vocabulary + IDF values
  4. Scaler mean and scale arrays
  5. Feature names list
"""

import os
import sys
import struct
import pickle
import json
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ARTIFACT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model_artifacts")
OUTPUT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model_artifacts", "model_params.bin")


def write_uint32(f, val):
    f.write(struct.pack("<I", val))

def write_float64(f, val):
    f.write(struct.pack("<d", val))

def write_float64_array(f, arr):
    flat = np.asarray(arr, dtype=np.float64).flatten()
    write_uint32(f, len(flat))
    f.write(flat.tobytes())

def write_string(f, s):
    encoded = s.encode("utf-8")
    write_uint32(f, len(encoded))
    f.write(encoded)

def write_string_list(f, lst):
    write_uint32(f, len(lst))
    for s in lst:
        write_string(f, s)


def main():
    print("Loading model artifacts...")

    # Load the MLP model
    with open(os.path.join(ARTIFACT_DIR, "model.pkl"), "rb") as f:
        model = pickle.load(f)

    # Load TF-IDF vectorizers
    with open(os.path.join(ARTIFACT_DIR, "tfidf_token.pkl"), "rb") as f:
        tfidf_token = pickle.load(f)
    with open(os.path.join(ARTIFACT_DIR, "tfidf_char.pkl"), "rb") as f:
        tfidf_char = pickle.load(f)

    # Load scaler
    with open(os.path.join(ARTIFACT_DIR, "scaler.pkl"), "rb") as f:
        scaler = pickle.load(f)

    # Load feature names
    with open(os.path.join(ARTIFACT_DIR, "feature_names.json"), "r") as f:
        feature_names = json.load(f)

    # Print model info
    print(f"  MLP layers: {len(model.coefs_)}")
    for i, (w, b) in enumerate(zip(model.coefs_, model.intercepts_)):
        print(f"    Layer {i}: weights {w.shape}, biases {b.shape}")

    token_vocab = tfidf_token.vocabulary_
    char_vocab = tfidf_char.vocabulary_
    print(f"  Token vocab size: {len(token_vocab)}")
    print(f"  Char vocab size: {len(char_vocab)}")
    print(f"  Scaler features: {len(scaler.mean_)}")
    print(f"  Handcrafted features: {len(feature_names)}")

    # Write binary file
    print(f"\nWriting to {OUTPUT_FILE}...")

    with open(OUTPUT_FILE, "wb") as f:
        # Magic header
        f.write(b"MLPV1\x00\x00\x00")  # 8 bytes magic

        # ========================================
        # Section 1: MLP weights and biases
        # ========================================
        num_layers = len(model.coefs_)
        write_uint32(f, num_layers)

        for i in range(num_layers):
            W = model.coefs_[i]     # shape: (in, out)
            b = model.intercepts_[i]  # shape: (out,)

            # Write dimensions
            write_uint32(f, W.shape[0])  # rows (input dim)
            write_uint32(f, W.shape[1])  # cols (output dim)

            # Write weight matrix (row-major)
            write_float64_array(f, W.flatten())

            # Write bias vector
            write_float64_array(f, b)

        # ========================================
        # Section 2: Token TF-IDF
        # ========================================
        # Sort vocabulary by index
        token_sorted = sorted(token_vocab.items(), key=lambda x: x[1])
        token_terms = [t[0] for t in token_sorted]
        token_idf = tfidf_token.idf_

        write_string_list(f, token_terms)
        write_float64_array(f, token_idf)

        # ========================================
        # Section 3: Char n-gram TF-IDF
        # ========================================
        char_sorted = sorted(char_vocab.items(), key=lambda x: x[1])
        char_terms = [t[0] for t in char_sorted]
        char_idf = tfidf_char.idf_

        write_string_list(f, char_terms)
        write_float64_array(f, char_idf)

        # ========================================
        # Section 4: Scaler parameters
        # ========================================
        write_float64_array(f, scaler.mean_)
        write_float64_array(f, scaler.scale_)

        # ========================================
        # Section 5: Feature names
        # ========================================
        write_string_list(f, feature_names)

    file_size = os.path.getsize(OUTPUT_FILE)
    print(f"  Written {file_size:,} bytes ({file_size / 1024 / 1024:.2f} MB)")
    print("  Done!")

    # Verification: run a quick forward pass in Python and print expected output
    print("\nVerification — Python predictions for test paths:")
    from train_classifier import LibraryFolderClassifier
    clf = LibraryFolderClassifier.load(ARTIFACT_DIR)
    test_paths = [
        "node_modules/express",
        "src/components",
        "__pycache__",
        "my-project",
        "@babel/core",
        "sprint-42-release",
    ]
    for p in test_paths:
        r = clf.predict(p)
        print(f"  {p:<30s} -> is_library={r['is_library']} prob={r['probability']:.6f}")


if __name__ == "__main__":
    main()
