#pragma once
#include <string>

struct FSConfig {
    bool dedup_enabled = false;
    std::string data_dir = "data";
};

extern FSConfig g_config;

bool load_config(const std::string path);