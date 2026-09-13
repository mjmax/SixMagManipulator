#include "FixtureIO.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
using namespace model_test;
using Clock=std::chrono::steady_clock;
namespace { volatile double sink=0; }
int main(int argc,char** argv)
{
    try {
        if (argc!=2) throw std::runtime_error("Usage: SixMagControlBenchmark reference_cases.csv");
        const auto cases=readCases(argv[1]);
        const MagneticModel model;
        ModelEvaluation result;
        double checksum=0;
        auto run=[&](std::size_t i) {
            const auto& c=cases[i%128];
            if (model.evaluate(c.r,c.theta,result)!=EvaluationStatus::Ok)
                throw std::runtime_error("Benchmark evaluation failed.");
            checksum+=result.acceleration[0]+result.positionJacobian[0][0]+result.angleJacobian[0][0];
        };
        for (std::size_t i=0;i<8192;++i) run(i);
        std::array<double,31> batches{};
        for (auto& us:batches) {
            const auto start=Clock::now();
            for (std::size_t i=0;i<128;++i) run(i);
            us=std::chrono::duration<double,std::micro>(Clock::now()-start).count()/128;
        }
        std::array<double,10000> samples{};
        for (std::size_t i=0;i<samples.size();++i) {
            const auto start=Clock::now();
            run(i);
            samples[i]=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
        }
        sink=checksum; // observable output: do not let the optimizer remove calls
        std::sort(batches.begin(),batches.end());
        const double mean=std::accumulate(samples.begin(),samples.end(),0.0)/samples.size();
        std::sort(samples.begin(),samples.end());
        std::cout<<std::fixed<<std::setprecision(6)
                 <<"Native g+Gr+Gth (also hn,Hn); double; 6 magnets x 144 sources.\n"
                 <<"31 warmed batches of 128 reference inputs, microseconds/call:\n"
                 <<"min "<<batches.front()<<", median "<<batches[15]<<", max "<<batches.back()<<'\n'
                 <<"10000 timed calls INCLUDING clock overhead and OS scheduling, microseconds:\n"
                 <<"mean "<<mean<<", median "<<samples[5000]<<", p95 "<<samples[9499]
                 <<", p99 "<<samples[9899]<<", max "<<samples.back()<<'\n'
                 <<"checksum "<<checksum<<"\nNot a worst-case or hard real-time guarantee.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
