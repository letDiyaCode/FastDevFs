/*
 * FastDevFs UI Module — Implementation
 */

#include "../include/ui.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace ui {

// ============================================================
// Global State
// ============================================================

static bool g_verbose = false;
static bool g_unicode_support = true;  // Auto-detect at runtime

// ============================================================
// Auto-detect Unicode Support
// ============================================================

static bool detect_unicode_support() {
    // Check LANG environment variable for UTF-8
    const char* lang = std::getenv("LANG");
    const char* lc_all = std::getenv("LC_ALL");
    
    if (lang && std::strstr(lang, "UTF-8")) return true;
    if (lc_all && std::strstr(lc_all, "UTF-8")) return true;
    
    // On Windows ConEmu, check if Unicode is available
    // Assume true on modern terminals
    return true;
}

// ============================================================
// ANSI Color Codes
// ============================================================

std::string color_code(Color c) {
    switch (c) {
        case Color::RED:    return "\033[91m";      // Bright red
        case Color::GREEN:  return "\033[92m";      // Bright green
        case Color::YELLOW: return "\033[93m";      // Bright yellow
        case Color::CYAN:   return "\033[96m";      // Bright cyan
        case Color::BOLD:   return "\033[1m";       // Bold
        case Color::RESET:  return "\033[0m";       // Reset
        default:            return "";
    }
}

std::string reset_code() {
    return "\033[0m";
}

std::string colored(const std::string& text, Color c) {
    return color_code(c) + text + reset_code();
}

std::string colored(const std::string& text, Color c1, Color c2) {
    return color_code(c1) + color_code(c2) + text + reset_code();
}

// ============================================================
// Unicode Symbol Support
// ============================================================

std::string symbol(Symbol s) {
    if (!g_unicode_support) {
        // ASCII fallback
        switch (s) {
            case Symbol::CHECK:     return "[OK]";
            case Symbol::CROSS:     return "[X]";
            case Symbol::GEAR:      return "[*]";
            case Symbol::FOLDER:    return "[D]";
            case Symbol::CHART:     return "[#]";
            case Symbol::ROCKET:    return "[>]";
            case Symbol::STOP:      return "[!]";
            case Symbol::HOURGLASS: return "[~]";
            case Symbol::ARROW:     return "->";
            case Symbol::BULLET:    return "*";
            default:                return "";
        }
    }

    // Unicode symbols
    switch (s) {
        case Symbol::CHECK:     return "✔";
        case Symbol::CROSS:     return "✖";
        case Symbol::GEAR:      return "⚙";
        case Symbol::FOLDER:    return "📁";
        case Symbol::CHART:     return "📊";
        case Symbol::ROCKET:    return "🚀";
        case Symbol::STOP:      return "🛑";
        case Symbol::HOURGLASS: return "⏳";
        case Symbol::ARROW:     return "→";
        case Symbol::BULLET:    return "•";
        default:                return "";
    }
}

bool supports_unicode() {
    return g_unicode_support;
}

void set_unicode_support(bool enable) {
    g_unicode_support = enable;
}

// ============================================================
// Text Formatting
// ============================================================

std::string format_kv(const std::string& key, const std::string& value,
                      int key_width) {
    std::ostringstream oss;
    oss << "  " << std::left << std::setw(key_width) << (key + ":") 
        << value;
    return oss.str();
}

std::string format_kv_colored(const std::string& key, const std::string& value,
                              Color value_color, int key_width) {
    std::ostringstream oss;
    oss << "  " << std::left << std::setw(key_width) << (key + ":") 
        << colored(value, value_color);
    return oss.str();
}

// ============================================================
// Status Messages
// ============================================================

void print_header(const std::string& title) {
    std::string header = colored("═══ " + title + " ═══", Color::CYAN, Color::BOLD);
    std::cout << "\n" << header << "\n" << std::endl;
}

void print_subheader(const std::string& title) {
    std::string header = colored("─── " + title + " ───", Color::CYAN);
    std::cout << "\n" << header << "\n" << std::endl;
}

void print_success(const std::string& msg) {
    std::string check = colored(symbol(Symbol::CHECK), Color::GREEN);
    std::cout << check << "  " << colored(msg, Color::GREEN) << std::endl;
}

void print_error(const std::string& msg) {
    std::string cross = colored(symbol(Symbol::CROSS), Color::RED);
    std::cout << cross << "  " << colored(msg, Color::RED) << std::endl;
}

void print_warning(const std::string& msg) {
    std::string warn = colored(symbol(Symbol::STOP), Color::YELLOW);
    std::cout << warn << "  " << colored(msg, Color::YELLOW) << std::endl;
}

void print_info(const std::string& msg) {
    std::string info = colored(symbol(Symbol::GEAR), Color::CYAN);
    std::cout << info << "  " << msg << std::endl;
}

void print_verbose(const std::string& msg) {
    if (g_verbose) {
        std::string dim = "  " + msg;  // Could add dim color in future
        std::cout << dim << std::endl;
    }
}

// ============================================================
// Tables and Sections
// ============================================================

void print_divider(int width) {
    std::string divider(width, '-');
    std::cout << colored(divider, Color::CYAN) << std::endl;
}

void print_status(const std::string& label, const std::string& value,
                  bool success) {
    Color status_color = success ? Color::GREEN : Color::RED;
    std::string sym = symbol(success ? Symbol::CHECK : Symbol::CROSS);
    sym = colored(sym, status_color);
    
    std::ostringstream oss;
    oss << "  " << std::left << std::setw(18) << (label + ":")
        << colored(value, status_color);
    
    std::cout << sym << "  " << std::left << std::setw(16) << label
              << " " << colored(value, status_color) << std::endl;
}

// ============================================================
// Interactive Elements
// ============================================================

void print_loading(const std::string& msg) {
    std::string hourglass = colored(symbol(Symbol::HOURGLASS), Color::YELLOW);
    std::cout << hourglass << "  " << msg << std::flush;
}

void print_loading_complete(const std::string& msg) {
    std::cout << "\r";  // Return to start of line
    print_success(msg);
}

void print_progress(const std::string& msg, int current, int total) {
    int percent = (total > 0) ? (current * 100) / total : 0;
    std::cout << msg << " [" << percent << "%]\r" << std::flush;
}

// ============================================================
// Utility
// ============================================================

void set_verbose(bool v) {
    g_verbose = v;
}

bool is_verbose() {
    return g_verbose;
}

void clear_line() {
    std::cout << "\33[2K\r" << std::flush;
}

}  // namespace ui

// For compatibility with older #include style
namespace {
    static bool _unicode_initialized = false;
    static void _ensure_unicode_init() {
        if (!_unicode_initialized) {
            ui::set_unicode_support(ui::supports_unicode());
            _unicode_initialized = true;
        }
    }
}
