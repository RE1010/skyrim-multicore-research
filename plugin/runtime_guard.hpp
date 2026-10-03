#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace runtime_guard {
inline bool hash_matches(const char* expected) {
    wchar_t filename[32768];if(!GetModuleFileNameW(nullptr,filename,32768)) return false;
    if(_wcsicmp(std::filesystem::path(filename).filename().c_str(),L"SkyrimSE.exe")) return false;
    std::ifstream file(std::filesystem::path(filename),std::ios::binary);if(!file) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    DWORD size=0,received=0;bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),4,&received,0)>=0;
    std::vector<unsigned char> object(size);unsigned char result[32]{};
    if(ok) ok=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
    char buffer[65536];
    while(ok && file.read(buffer,sizeof(buffer))) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),sizeof(buffer),0)>=0;
    if(ok && file.gcount()) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),static_cast<ULONG>(file.gcount()),0)>=0;
    if(file.bad()) ok=false;
    if(ok) ok=BCryptFinishHash(hash,result,sizeof(result),0)>=0;
    if(hash) BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    const char digits[]="0123456789ABCDEF";std::string hex;
    for(const auto byte:result) {hex+=digits[byte>>4];hex+=digits[byte&15];}
    return ok && hex==expected;
}
}
