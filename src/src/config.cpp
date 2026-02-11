#include "config.h"
#include <fstream>
#include <sstream>
#include<iostream>
FSConfig g_config;

bool load_config(const std::string path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr<<"In load_config: Could not open file.";
        return false;
    }
    std::string line;

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string key, value;

        if (std::getline(iss, key, '=') &&
            std::getline(iss, value))
        {
            if (key == "dedup")
                g_config.dedup_enabled = (value == "true");

            if (key == "data_dir")
                g_config.data_dir = value;
        }
    }

    return true;
}