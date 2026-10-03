#include "multicore/visibility.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
using namespace multicore;
using Clock=std::chrono::steady_clock;
int main() {
    const Frustum frustum({{{1,0,0,100},{-1,0,0,100},{0,1,0,100},{0,-1,0,100},{0,0,1,100},{0,0,-1,100}}});
    std::mt19937 random(174104);
    std::uniform_real_distribution<float> coordinate(-150,150), radius(0,10);
    std::cout << "objects,workers,includes_snapshot_copy,repetitions,median_ms,p95_ms,checksum\n";
    for (std::size_t count : {1024u,16384u,65536u,262144u}) {
        std::vector<Sphere> source(count), snapshot(count);
        for(auto& s:source) s={coordinate(random),coordinate(random),coordinate(random),radius(random)};
        std::vector<std::uint8_t> expected(count),output(count);
        classify_serial(source,frustum,expected);
        struct Mode {std::size_t workers; bool copy; std::vector<double> timings;};
        std::vector<std::unique_ptr<VisibilityWorkers>> pools;
        std::vector<Mode> modes;
        for (std::size_t workers : {0u,2u,4u,6u}) {
            pools.push_back(std::make_unique<VisibilityWorkers>(workers,0));
            modes.push_back({workers,false,{}}); modes.push_back({workers,true,{}});
        }
        // Rotate the modes per trial to avoid always measuring serial first.
        for (std::size_t trial=0; trial!=110; ++trial) {
            for (std::size_t step=0; step!=modes.size(); ++step) {
                    const auto mode_index=(trial+step)%modes.size();
                    auto& mode=modes[mode_index];
                    const auto start=Clock::now();
                    if(mode.copy) std::copy(source.begin(),source.end(),snapshot.begin());
                    const auto& input=mode.copy?snapshot:source;
                    if(mode.workers) pools[mode_index/2]->classify(input,frustum,output); else classify_serial(input,frustum,output);
                    const auto end=Clock::now();
                    if(output!=expected) {std::cerr<<"Mismatch\n";return 1;}
                    if(trial>=10) mode.timings.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
        }
        for (auto& mode:modes) {
            std::sort(mode.timings.begin(),mode.timings.end());
            std::cout<<count<<','<<mode.workers<<','<<(mode.copy?1:0)<<",100,"<<std::fixed<<std::setprecision(6)
                     <<mode.timings[50]<<','<<mode.timings[95]<<','
                     <<std::accumulate(output.begin(),output.end(),std::size_t(0))<<'\n';
        }
    }
}
