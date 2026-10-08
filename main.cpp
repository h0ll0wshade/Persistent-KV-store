#include "kvstore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {
bool isNumberedLog(const std::filesystem::path& path) {
    if (path.extension() != ".log") return false;
    const std::string id = path.stem().string();
    return id.size() >= 6 &&
           std::all_of(id.begin(), id.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

void showLogs(const std::string& directory) {
    bool found = false;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file() || !isNumberedLog(item.path())) continue;
        std::cout << item.path().filename().string() << "  "
                  << std::filesystem::file_size(item.path()) << " bytes\n";
        found = true;
    }
    if (!found) std::cout << "(no log files)\n";
}

void printHelp() {
    std::cout << "Commands:\n"
              << "  put <key> <value>  Store a value (value may contain spaces)\n"
              << "  get <key>          Read a value\n"
              << "  erase <key>        Delete a key\n"
              << "  list               List live keys\n"
              << "  logs               Show numbered log files and sizes\n"
              << "  sync               Flush log files to disk\n"
              << "  help               Show this help\n"
              << "  quit               Sync and exit\n";
}
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: bitcask_demo <database-directory>\n";
        return 2;
    }

    try {
        KVStore store(argv[1]);
        std::cout << "Opened database: " << argv[1] << "\n";
        printHelp();

        std::string line;
        while (true) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) break;

            std::istringstream command_line(line);
            std::string command;
            if (!(command_line >> command)) continue;
            if (command == "quit") break;
            if (command == "help") {
                printHelp();
                continue;
            }

            try {
                if (command == "put") {
                    std::string key;
                    std::string value;
                    if (!(command_line >> key)) {
                        std::cout << "Usage: put <key> <value>\n";
                        continue;
                    }
                    std::getline(command_line, value);
                    const auto first_value_char = value.find_first_not_of(" \t");
                    if (first_value_char == std::string::npos) value.clear();
                    else value.erase(0, first_value_char);
                    store.put(key, value);
                    std::cout << "OK\n";
                } else if (command == "get") {
                    std::string key;
                    if (!(command_line >> key)) {
                        std::cout << "Usage: get <key>\n";
                        continue;
                    }
                    const auto value = store.get(key);
                    if (value) std::cout << *value << '\n';
                    else std::cout << "Key not found\n";
                } else if (command == "erase") {
                    std::string key;
                    if (!(command_line >> key)) {
                        std::cout << "Usage: erase <key>\n";
                        continue;
                    }
                    store.erase(key);
                    std::cout << "OK\n";
                } else if (command == "list") {
                    const auto keys = store.listKeys();
                    if (keys.empty()) std::cout << "(no keys)\n";
                    else for (const auto& key : keys) std::cout << key << '\n';
                } else if (command == "logs") {
                    showLogs(argv[1]);
                } else if (command == "sync") {
                    store.sync();
                    std::cout << "Synced\n";
                } else {
                    std::cout << "Unknown command. Type 'help' for commands.\n";
                }
            } catch (const std::exception& error) {
                std::cerr << "Operation failed: " << error.what() << '\n';
            }
        }

        store.sync();
        std::cout << "Synced; closing database.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Could not open or close database: " << error.what() << '\n';
        return 1;
    }
}
