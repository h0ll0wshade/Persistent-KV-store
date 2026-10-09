#pragma once

#include <string>

// Make directory-entry changes durable on POSIX filesystems.
void syncDirectory(const std::string& path);
