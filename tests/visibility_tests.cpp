#include "multicore/visibility.hpp"
#include <algorithm>
#include <future>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
using namespace multicore;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::array<Plane,6> box() { return {{{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}}}; }
int main() {
    try {
        const Frustum f(box());
        require(f.may_be_visible({0,0,0,0}), "inside point");
        require(!f.may_be_visible({2,0,0,0}), "outside point");
        require(f.may_be_visible({2,0,0,1}), "tangent sphere must survive");
        require(!f.may_be_visible({2.01f,0,0,1}), "separated sphere");
        require(f.may_be_visible({100,100,100,-1}), "invalid radius must survive");
        require(f.may_be_visible({std::numeric_limits<float>::quiet_NaN(),0,0,0}), "NaN bound");
        auto scaled = box(); for (auto& p : scaled) {p.x*=2; p.y*=2; p.z*=2; p.offset*=2;}
        require(Frustum(scaled).may_be_visible({2,0,0,1}), "unnormalized plane");
        require(Frustum(box(),0).may_be_visible({100,100,100,0}), "disabled planes");
        require(Frustum(box(),1).may_be_visible({100,100,100,0}), "active plane mask");
        bool rejected = false;
        try { auto invalid=box(); invalid[0]={0,0,0,1}; const Frustum bad(invalid); }
        catch (const std::invalid_argument&) {rejected=true;}
        require(rejected, "invalid plane rejection");
        std::mt19937 random(174104);
        std::uniform_real_distribution<float> coordinate(-5,5), radius(0,2);
        std::vector<Sphere> data(32771);
        for (auto& s : data) s={coordinate(random),coordinate(random),coordinate(random),radius(random)};
        std::vector<std::uint8_t> expected(data.size()), actual(data.size());
        classify_serial(data,f,expected);
        for (std::size_t count : {0u,1u,2u,4u,8u}) {
            VisibilityWorkers pool(count,0);
            for (std::size_t size : {0u,1u,3u,65u,32771u}) {
                const auto input=std::span<const Sphere>(data).first(size);
                const auto output=std::span<std::uint8_t>(actual).first(size);
                for (int repeat=0; repeat!=20; ++repeat) {
                    pool.classify(input,f,output);
                    require(std::equal(output.begin(),output.end(),expected.begin()), "parallel mismatch or stale result");
                }
            }
        }
        VisibilityWorkers shared(4,0);
        auto caller=[&]{std::vector<std::uint8_t> result(data.size()); for(int i=0;i!=30;++i) {
            shared.classify(data,f,result); require(result==expected,"concurrent caller mismatch");}};
        auto a=std::async(std::launch::async,caller); auto b=std::async(std::launch::async,caller);
        a.get(); b.get();
        VisibilityWorkers fallback(4,data.size()+1);
        fallback.classify(data,f,actual); require(actual==expected,"serial fallback");
        rejected=false;
        try {shared.classify(data,f,std::span<std::uint8_t>(actual).first(1));}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"output size rejection");
        std::cout << "PASS: geometry, boundary, masks, invalid inputs, persistent batches, 0/1/2/4/8 workers, concurrent callers and fallback\n";
        return 0;
    } catch (const std::exception& e) {std::cerr << e.what() << '\n'; return 1;}
}
