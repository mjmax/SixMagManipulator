#include "FixtureIO.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <thread>

namespace {
std::atomic<std::size_t> allocations{0};
thread_local bool trackAllocations=false;
}
void* operator new(std::size_t n)
{
    if (trackAllocations) ++allocations;
    if (void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

using namespace model_test;
namespace {
void require(bool good,const char* message)
{
    if (!good) throw std::runtime_error(message);
}
ModelEvaluation evaluate(const MagneticModel& m,const Vector2& r,const MagnetAngles& th)
{
    ModelEvaluation out;
    require(m.evaluate(r,th,out)==EvaluationStatus::Ok,"Unexpected evaluation failure.");
    return out;
}
bool close(double a,double b,double atol=1e-10,double rtol=2e-10)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=atol+rtol*std::abs(b);
}
void checkFailure(const MagneticModel& model,const Vector2& r,
                  const MagnetAngles& th,EvaluationStatus expected)
{
    ModelEvaluation out;
    require(model.evaluate(r,th,out)==expected,"Incorrect failure status.");
    for (double v:flatten(out)) require(std::isnan(v),"Failure did not invalidate every output.");
}
}
int main(int argc,char** argv)
{
    try {
        require(argc==2,"Usage: SixMagControlTests reference_cases.csv");
        const auto cases=readCases(argv[1]);
        const MagneticModel model;
        constexpr std::array<std::size_t,5> widths{2,4,12,2,4};
        constexpr const char* labels[]{"g","Gr","Gth","hn","Hn"};
        std::array<double,5> maxAbs{},maxScaled{};
        for (std::size_t k=0;k<cases.size();++k) {
            const auto& c=cases[k];
            const auto actual=flatten(evaluate(model,c.r,c.theta));
            std::size_t first=0;
            for (std::size_t group=0;group<widths.size();++group) {
                double error=0, norm=0;
                for (std::size_t i=first;i<first+widths[group];++i) {
                    error=std::hypot(error,actual[i]-c.expected[i]);
                    norm=std::hypot(norm,c.expected[i]);
                }
                if (!(error<=1e-10+2e-10*norm))
                    throw std::runtime_error("MATLAB mismatch, case "+std::to_string(k)+
                                             ", output "+labels[group]);
                maxAbs[group]=std::max(maxAbs[group],error);
                maxScaled[group]=std::max(maxScaled[group],error/std::max(1.0,norm));
                first+=widths[group];
            }
        }
        std::cout<<"PASS: "<<cases.size()<<" direct __refmod cases (SI, double).\n";
        for (std::size_t i=0;i<widths.size();++i)
            std::cout<<labels[i]<<": max abs norm "<<maxAbs[i]
                     <<", max error/max(1,norm) "<<maxScaled[i]<<'\n';

        double maxFdError=0;
        for (std::size_t k=0;k<32;++k) {
            const auto& c=cases[k];
            const auto base=evaluate(model,c.r,c.theta);
            for (std::size_t j=0;j<2;++j) {
                auto plus=c.r,minus=c.r; plus[j]+=1e-7; minus[j]-=1e-7;
                const auto a=evaluate(model,plus,c.theta),b=evaluate(model,minus,c.theta);
                for (std::size_t i=0;i<2;++i) {
                    const double fd=(a.acceleration[i]-b.acceleration[i])/2e-7;
                    require(close(base.positionJacobian[i][j],fd,1e-5,2e-5),"Position finite difference failed.");
                    maxFdError=std::max(maxFdError,std::abs(base.positionJacobian[i][j]-fd)/std::max(1.0,std::abs(fd)));
                }
            }
            for (std::size_t j=0;j<6;++j) {
                auto plus=c.theta,minus=c.theta; plus[j]+=1e-6; minus[j]-=1e-6;
                const auto a=evaluate(model,c.r,plus),b=evaluate(model,c.r,minus);
                for (std::size_t i=0;i<2;++i) {
                    const double fd=(a.acceleration[i]-b.acceleration[i])/2e-6;
                    require(close(base.angleJacobian[i][j],fd,1e-5,2e-5),"Angle finite difference failed.");
                    maxFdError=std::max(maxFdError,std::abs(base.angleJacobian[i][j]-fd)/std::max(1.0,std::abs(fd)));
                }
            }
            auto wrapped=c.theta;
            for (auto& t:wrapped) t+=2*3.14159265358979323846;
            const auto a=flatten(base),b=flatten(evaluate(model,c.r,wrapped));
            for (std::size_t i=0;i<a.size();++i)
                require(close(a[i],b[i]),"Angle periodicity failed.");
        }
        std::cout<<"PASS: 32 independent derivative/periodicity cases; max scaled FD error "
                 <<maxFdError<<'\n';

        auto p=MagneticModel::referenceParameters();
        p.kg*=4;
        const MagneticModel scaled(p);
        for (std::size_t k=0;k<16;++k) {
            const auto a=flatten(evaluate(model,cases[k].r,cases[k].theta));
            const auto b=flatten(evaluate(scaled,cases[k].r,cases[k].theta));
            for (std::size_t i=0;i<a.size();++i)
                require(close(b[i],a[i]*(i<18?4:2)),"Parameter scaling failed.");
        }
        p.kg=0;
        bool rejected=false;
        try { MagneticModel invalid(p); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid initialization was accepted.");
        p=MagneticModel::referenceParameters();
        p.coefficients[0]=std::numeric_limits<double>::quiet_NaN();
        rejected=false;
        try { MagneticModel invalid(p); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Nonfinite source was accepted.");
        const MagnetAngles zeros{};
        checkFailure(model,{std::numeric_limits<double>::quiet_NaN(),0},zeros,EvaluationStatus::InvalidInput);
        auto badAngles=zeros; badAngles[4]=std::numeric_limits<double>::infinity();
        checkFailure(model,{0,0},badAngles,EvaluationStatus::InvalidInput);
        checkFailure(model,{1e300,1e300},zeros,EvaluationStatus::NonFiniteResult);
        p=MagneticModel::referenceParameters();
        p.magnetX[0]=p.magnetY[0]=p.placementAngles[0]=0;
        p.sourceX[0]=p.sourceY[0]=p.sourceZ[0]=0;
        checkFailure(MagneticModel(p),{0,0},zeros,EvaluationStatus::SingularSource);
        std::cout<<"PASS: parameter changes, invalid inputs, overflow, and singular source rejection.\n";

        allocations=0;
        bool success=true;
        trackAllocations=true;
        for (std::size_t k=0;k<4096;++k) {
            ModelEvaluation out;
            const auto& c=cases[k%cases.size()];
            if (model.evaluate(c.r,c.theta,out)!=EvaluationStatus::Ok) success=false;
        }
        trackAllocations=false;
        require(success && allocations==0,"Evaluation allocated memory or failed.");
        std::cout<<"PASS: 4096 evaluations with zero C++ new/new[] allocations.\n";
        std::atomic<bool> concurrentOk{true};
        std::array<std::thread,4> workers;
        for (auto& worker:workers) worker=std::thread([&] {
            for (std::size_t k=0;k<256;++k) {
                ModelEvaluation a,b;
                if (model.evaluate(cases[k].r,cases[k].theta,a)!=EvaluationStatus::Ok ||
                    model.evaluate(cases[k].r,cases[k].theta,b)!=EvaluationStatus::Ok ||
                    flatten(a)!=flatten(b)) concurrentOk=false;
                const auto expected=flatten(evaluate(MagneticModel(),cases[k].r,cases[k].theta));
                if (flatten(a)!=expected) concurrentOk=false;
            }
        });
        for (auto& worker:workers) worker.join();
        require(concurrentOk,"Concurrent evaluation was not deterministic.");
        std::cout<<"PASS: shared-model concurrent evaluation on 4 threads.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
