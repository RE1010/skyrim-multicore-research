#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace report_file {
// False means a reader/scanner blocked replacement; retry after the next update.
inline bool publish(const std::filesystem::path& temporary,const std::filesystem::path& target) {
    if(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING)) return true;
    const auto error=GetLastError();
    if(error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION || error==ERROR_ACCESS_DENIED) return false;
    throw std::runtime_error("Cannot publish diagnostic summary: "+std::to_string(error));
}
}
