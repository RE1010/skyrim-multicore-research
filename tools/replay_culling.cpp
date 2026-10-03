#include "multicore/engine_shadow.hpp"
#include "runtime_evidence.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("Usage: replay_culling snapshots.bin");
        std::ifstream input(argv[1],std::ios::binary);
        multicore::SnapshotHeader header{};
        if(!input.read(reinterpret_cast<char*>(&header),sizeof(header)) || std::memcmp(header.magic,"MCVS1710",8)
            || header.record_size!=sizeof(multicore::EngineSnapshot) || header.runtime_version!=runtime_evidence::version
            || std::memcmp(header.exe_sha256,runtime_evidence::sha256,64)) throw std::runtime_error("Invalid snapshot header");
        multicore::VisibilityWorkers workers(4,0);
        multicore::ShadowStats totals{};
        std::vector<multicore::EngineSnapshot> batch(8192);
        while(input) {
            input.read(reinterpret_cast<char*>(batch.data()),static_cast<std::streamsize>(batch.size()*sizeof(batch[0])));
            const auto bytes=input.gcount();
            if(bytes%sizeof(batch[0]) || input.bad()) throw std::runtime_error("Truncated snapshot record");
            if(!bytes) break;
            const auto part=multicore::compare_engine_snapshots({batch.data(),static_cast<std::size_t>(bytes)/sizeof(batch[0])},workers);
            totals.compared+=part.compared;totals.unsupported+=part.unsupported;
            totals.visibility_mismatches+=part.visibility_mismatches;totals.mask_mismatches+=part.mask_mismatches;
            totals.serial_parallel_mismatches+=part.serial_parallel_mismatches;
        }
        std::cout<<"{\"compared\":"<<totals.compared<<",\"unsupported\":"<<totals.unsupported
            <<",\"visibilityMismatches\":"<<totals.visibility_mismatches<<",\"maskMismatches\":"<<totals.mask_mismatches
            <<",\"serialParallelMismatches\":"<<totals.serial_parallel_mismatches<<"}\n";
        return (!totals.compared || totals.unsupported || totals.visibility_mismatches || totals.mask_mismatches
            || totals.serial_parallel_mismatches)?2:0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
