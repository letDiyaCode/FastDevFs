/*
 * FastDevFs UI Module — Visual styling and formatting utilities.
 *
 * Provides:
 * - ANSI color codes and text formatting
 * - Unicode symbols (with plain-text fallback)
 * - Key-value pair formatting with alignment
 * - Section headers and dividers
 * - Success/error/warning messages
 * - Status indicators
 *
 * Designed to be lightweight, terminal-agnostic, and modular.
 */

#pragma once

#include <string>
#include <vector>
#include <utility>

namespace ui {

// ============================================================
// Color and Style Codes (ANSI escape sequences)
// ============================================================

enum class Color {
    RESET = 0,
    RED = 1,
    GREEN = 2,
    YELLOW = 3,
    CYAN = 4,
    BOLD = 5,
};

// Get ANSI escape code for a color
std::string color_code(Color c);

// Reset all formatting
std::string reset_code();

// Apply color to text
std::string colored(const std::string& text, Color c);
std::string colored(const std::string& text, Color c1, Color c2);  // Two colors

// ============================================================
// Unicode Symbols (with fallback to ASCII)
// ============================================================

enum class Symbol {
    CHECK,          // ✔ / [OK]
    CROSS,          // ✖ / [X]
    GEAR,           // ⚙ / [*]
    FOLDER,         // 📁 / [D]
    CHART,          // 📊 / [#]
    ROCKET,         // 🚀 / [>]
    STOP,           // 🛑 / [!]
    HOURGLASS,      // ⏳ / [~]
    ARROW,          // → / ->
    BULLET,         // • / *
};

// Get symbol string (with ASCII fallback)
// Call set_unicode_support() once at startup
std::string symbol(Symbol s);

// Check if terminal supports Unicode
bool supports_unicode();

// Manually set Unicode support
void set_unicode_support(bool enable);

// ============================================================
// Text Formatting
// ============================================================

// Format a key-value pair with alignment
// Example: "  Status        : RUNNING"
std::string format_kv(const std::string& key, const std::string& value,
                      int key_width = 18);

// Format with colored value
std::string format_kv_colored(const std::string& key, const std::string& value,
                              Color value_color, int key_width = 18);

// ============================================================
// Status Messages
// ============================================================

// Print header with section title
void print_header(const std::string& title);

// Print sub-header (less prominent)
void print_subheader(const std::string& title);

// Print success message
void print_success(const std::string& msg);

// Print error message
void print_error(const std::string& msg);

// Print warning message
void print_warning(const std::string& msg);

// Print info message
void print_info(const std::string& msg);

// Print verbose-only message (respects verbosity level)
void print_verbose(const std::string& msg);

// ============================================================
// Tables and Sections
// ============================================================

// Section divider
void print_divider(int width = 40);

// Status indicator with label and value
// Example: "✔ Running        : true"
void print_status(const std::string& label, const std::string& value,
                  bool success);

// ============================================================
// Interactive Elements
// ============================================================

// Loading spinner message
void print_loading(const std::string& msg);

// Loading complete
void print_loading_complete(const std::string& msg);

// Progress indicator (simple percentage)
void print_progress(const std::string& msg, int current, int total);

// ============================================================
// Utility
// ============================================================

// Set global verbose mode
void set_verbose(bool v);

// Check if verbose mode is enabled
bool is_verbose();

// Clear line (useful for spinners)
void clear_line();

}  // namespace ui
