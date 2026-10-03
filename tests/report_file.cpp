#define NOMINMAX
#include "report_file.hpp"
#include <fstream>
#include <iostream>
int main() {
    wchar_t temp_directory[MAX_PATH],unique[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,temp_directory) || !GetTempFileNameW(temp_directory,L"mcv",0,unique)) return 1;
    const std::filesystem::path target(unique),temporary=std::wstring(unique)+L".replacement";
    {std::ofstream out(target);out<<"old";}
    {std::ofstream out(temporary);out<<"new";}
    const auto reader=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(reader==INVALID_HANDLE_VALUE) return 2;
    const bool incorrectly_published=report_file::publish(temporary,target);
    const bool retained=std::filesystem::exists(temporary);
    CloseHandle(reader);
    if(incorrectly_published || !retained || !report_file::publish(temporary,target)) return 3;
    std::string result;{std::ifstream in(target);in>>result;}
    DeleteFileW(target.c_str());
    if(result!="new") return 4;
    std::cout<<"PASS: a reader denying file replacement defers publication; retry publishes after reader closes\n";
    return 0;
}
