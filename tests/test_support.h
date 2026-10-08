#pragma once

#include <filesystem>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unistd.h>

inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class TempDirectory {
public:
    TempDirectory() {
        std::string writable = (std::filesystem::temp_directory_path() / "bitcask-test-XXXXXX").string();
        writable.push_back('\0');
        char* result = ::mkdtemp(writable.data());
        if (result == nullptr) throw std::runtime_error("mkdtemp failed");
        path_ = result;
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};
