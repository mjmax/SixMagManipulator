#include "ControlLoop.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace sixmag::control;
namespace { std::atomic<std::size_t> allocations{0}; thread_local bool track=false; }
void* operator new(std::size_t n) {
    if (track) ++allocations;
    if (void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
namespace {
struct Case { Vector2 r{}; MagnetAngles theta{},u{}; };
void require(bool good,const char* message) { if (!good) throw std::runtime_error(message); }
bool close(double a,double b) { return std::isfinite(a) && std::abs(a-b)<=1e-10+2e-10*std::abs(b); }
std::vector<Case> readCases(const char* path,double& damping)
{
    std::ifstream file(path); require(bool(file),"Cannot read control fixtures.");
    std::vector<Case> cases; std::string line;
    while (std::getline(file,line)) {
        if (line.rfind("# sigma_v=",0)==0) damping=std::stod(line.substr(10));
        if (line.empty() || line[0]=='#') continue;
        std::istringstream stream(line); std::string value;
        std::array<double,14> data{}; std::size_t n=0;
        while (std::getline(stream,value,',')) {
            require(n<data.size(),"Too many control fixture columns.");
            data[n++]=std::stod(value); require(std::isfinite(data[n-1]),"Nonfinite fixture.");
        }
        require(n==14,"Missing control fixture columns.");
        Case c; c.r={data[0],data[1]};
        for (std::size_t j=0;j<6;++j) { c.theta[j]=data[2+j]; c.u[j]=data[8+j]; }
        cases.push_back(c);
    }
    require(cases.size()==265 && std::isfinite(damping) && damping>0,"Incomplete fixture file.");
    return cases;
}
ControlFeedback feedback(const Case& c,std::uint64_t sequence)
{
    ControlFeedback f; f.position=c.r; f.measuredAngles=c.theta;
    f.positionValid=true; f.anglesValid=true;
    f.positionSequence=sequence; f.positionTimeSeconds=static_cast<double>(sequence)*.001;
    return f;
}
void checkNoCommand(const ControlStep& out,ControlStepStatus status)
{
    require(out.status==status && !out.commandReady,"Incorrect rejection status.");
    for (double v:out.commandedAngles) require(std::isnan(v),"Rejected command not invalidated.");
}
volatile double benchmarkSink=0;
void benchmark(const MagneticModel& model,const std::vector<Case>& cases)
{
    const LinearizedPositionControl law(model);
    ControlLoop loop(model); require(loop.selectMode(ControlMode::LinearizedOrigin),"Mode rejected.");
    using Clock=std::chrono::steady_clock;
    std::array<double,7> raw{},guarded{};
    constexpr std::size_t count=100000;
    std::uint64_t sequence=0; double checksum=0;
    for (std::size_t batch=0;batch<10;++batch) {
        auto start=Clock::now();
        for (std::size_t i=0;i<count;++i) {
            MagnetAngles out;
            if (law.evaluate(cases[i&255].r,out)!=ControlLawStatus::Ok) throw std::runtime_error("Law benchmark failed.");
            checksum+=out[0];
        }
        const double a=std::chrono::duration<double,std::micro>(Clock::now()-start).count()/count;
        start=Clock::now();
        for (std::size_t i=0;i<count;++i) {
            auto f=feedback(cases[i&255],sequence++);
            const auto out=loop.step(f,f.positionTimeSeconds);
            if (!out.commandReady) throw std::runtime_error("Step benchmark failed.");
            checksum+=out.commandedAngles[0];
        }
        const double b=std::chrono::duration<double,std::micro>(Clock::now()-start).count()/count;
        if (batch>=3) { raw[batch-3]=a; guarded[batch-3]=b; }
    }
    benchmarkSink=checksum;
    std::sort(raw.begin(),raw.end()); std::sort(guarded.begin(),guarded.end());
    std::cout<<std::fixed<<std::setprecision(6)
        <<"Control-law warmed batch median: "<<raw[3]<<" us/update.\n"
        <<"Complete computational step median: "<<guarded[3]<<" us/update; range "
        <<guarded.front()<<".."<<guarded.back()<<" us.\n"
        <<"Includes input preparation/loop overhead; excludes initialization and all device I/O.\n";
}
}
int main(int argc,char** argv)
{
    try {
        require(argc==2 || argc==3,"Usage: SixMagControllerTests fixtures.csv [--benchmark]");
        double damping=0; const auto cases=readCases(argv[1],damping);
        const MagneticModel model; const LinearizedPositionControl law(model);
        ControlLoop loop(model); const ControlLoopOptions defaults;
        checkNoCommand(loop.step(feedback(cases[0],0),0),ControlStepStatus::Disabled);
        require(loop.selectMode(ControlMode::LinearizedOrigin),"Cannot select linear controller.");
        ModelEvaluation origin; require(model.evaluate({}, {},origin)==EvaluationStatus::Ok,"Bad origin.");
        double maxError=0; std::size_t saturatedCases=0;
        for (std::size_t k=0;k<cases.size();++k) {
            const auto& c=cases[k]; MagnetAngles u;
            require(law.evaluate(c.r,u)==ControlLawStatus::Ok,"Control law failed.");
            auto f=feedback(c,k); const auto out=loop.step(f,f.positionTimeSeconds);
            require(out.commandReady && out.status==ControlStepStatus::Ready,"Loop did not produce command.");
            bool limited=false;
            for (std::size_t j=0;j<6;++j) {
                require(close(u[j],c.u[j]),"MATLAB control.m comparison failed.");
                maxError=std::max(maxError,std::abs(u[j]-c.u[j]));
                require(out.requestedAngles[j]==u[j],"Loop changed unrestricted control law.");
                const double expected=std::clamp(u[j],defaults.lowerAngles[j],defaults.upperAngles[j]);
                require(out.commandedAngles[j]==expected,"Incorrect angle limit.");
                require(out.limited[j]==(u[j]!=expected),"Incorrect saturation flag.");
                limited|=out.limited[j];
            }
            saturatedCases+=limited;
            for (std::size_t i=0;i<2;++i) {
                double achieved=0;
                for (std::size_t j=0;j<6;++j) achieved+=origin.angleJacobian[i][j]*u[j];
                require(close(achieved,-4000*c.r[i]),"Pseudoinverse equation failed.");
            }
        }
        std::cout<<"PASS: "<<cases.size()<<" original control.m cases; max angle error "<<maxError<<" rad.\n"
                 <<"Saturated cases with default limits: "<<saturatedCases<<"/"<<cases.size()<<".\n";
        double radius=std::numeric_limits<double>::infinity();
        for (const auto& row:law.matrix()) radius=std::min(radius,defaults.upperAngles[0]/std::hypot(row[0],row[1]));
        std::cout<<"Inscribed unsaturated position radius: "<<radius<<" m.\n";
        loop.reset(); auto f=feedback(cases[0],10);
        require(loop.step(f,f.positionTimeSeconds).commandReady,"Valid feedback rejected.");
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::NoNewMeasurement);
        checkNoCommand(loop.step(f,1),ControlStepStatus::StaleMeasurement);
        f.positionSequence=9;
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::OutOfOrderMeasurement);
        f.positionSequence=11; f.positionTimeSeconds=.009;
        checkNoCommand(loop.step(f,.011),ControlStepStatus::OutOfOrderMeasurement);
        f.positionTimeSeconds=2;
        checkNoCommand(loop.step(f,1),ControlStepStatus::InvalidTime);
        f=feedback(cases[0],12); f.positionValid=false;
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::InvalidMeasurement);
        f.positionValid=true; f.position[0]=std::numeric_limits<double>::quiet_NaN();
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::InvalidMeasurement);
        f=feedback(cases[0],13); f.position[0]=std::numeric_limits<double>::max();
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::CalculationFailed);
        f=feedback(cases[0],14); f.anglesValid=false; f.measuredAngles.fill(std::numeric_limits<double>::quiet_NaN());
        require(loop.step(f,f.positionTimeSeconds).commandReady,"Position-only law incorrectly requires angle feedback.");
        require(!loop.selectMode(static_cast<ControlMode>(99)),"Unsupported mode accepted.");
        checkNoCommand(loop.step(f,f.positionTimeSeconds),ControlStepStatus::Disabled);
        require(loop.selectMode(ControlMode::LinearizedOrigin),"Mode selection failed.");
        require(loop.step(f,f.positionTimeSeconds).commandReady,"Mode switch did not reset sample history.");
        std::cout<<"PASS: disabled/mode changes, missing/stale/duplicate/out-of-order input, invalid time, NaN and overflow.\n";
        LinearizedPositionControl half(model,2000); MagnetAngles u;
        require(half.evaluate(cases[0].r,u)==ControlLawStatus::Ok,"Custom gain failed.");
        for (std::size_t j=0;j<6;++j) require(close(u[j],cases[0].u[j]*.5),"Custom gain mismatch.");
        auto p=MagneticModel::referenceParameters(); p.coefficients.fill(0);
        bool rejected=false;
        try { LinearizedPositionControl invalid{MagneticModel(p)}; }
        catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Rank-deficient initialization accepted.");
        auto options=defaults; options.lowerAngles[2]=.25; options.upperAngles[2]=.5;
        ControlLoop custom(model,options); require(custom.selectMode(ControlMode::LinearizedOrigin),"Custom mode rejected.");
        Case zero{}; const auto bounded=custom.step(feedback(zero,0),0);
        require(bounded.commandReady && bounded.commandedAngles[2]==.25 && bounded.limited[2],"Per-motor limits ignored.");
        options.maximumPositionAge=0; rejected=false;
        try { ControlLoop invalid(model,options); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid freshness limit accepted.");
        options=defaults; options.lowerAngles[0]=4; rejected=false;
        try { ControlLoop invalid(model,options); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid angle limits accepted.");
        std::cout<<"PASS: configurable gain/limits and bad initialization rejection.\n";
        // Offline linear plant, instantaneous actuation, reference damping.
        ControlLoop simulated(model); require(simulated.selectMode(ControlMode::LinearizedOrigin),"Mode rejected.");
        Vector2 r{.001,-.00075},v{}; constexpr double dt=.001;
        for (std::uint64_t k=0;k<2000;++k) {
            ControlFeedback state; state.position=r; state.positionValid=true;
            state.positionSequence=k; state.positionTimeSeconds=static_cast<double>(k)*dt;
            const auto command=simulated.step(state,state.positionTimeSeconds);
            require(command.commandReady,"Simulation command failed.");
            Vector2 a{};
            for (std::size_t i=0;i<2;++i) {
                a[i]=origin.positionJacobian[i][0]*r[0]+origin.positionJacobian[i][1]*r[1]-damping*v[i];
                for (std::size_t j=0;j<6;++j) a[i]+=origin.angleJacobian[i][j]*command.commandedAngles[j];
            }
            for (std::size_t i=0;i<2;++i) { v[i]+=dt*a[i]; r[i]+=dt*v[i]; }
        }
        require(std::hypot(r[0],r[1])<1e-6,"Offline linear-plant convergence failed.");
        std::cout<<"PASS: offline linear-plant smoke test; final radius "<<std::hypot(r[0],r[1])<<" m.\n";
        loop.reset(); allocations=0; bool success=true; track=true;
        for (std::uint64_t k=0;k<4096;++k) {
            const auto state=feedback(cases[k&255],k);
            if (!loop.step(state,state.positionTimeSeconds).commandReady) success=false;
        }
        track=false;
        require(success && allocations==0,"Control step allocated or failed.");
        std::cout<<"PASS: 4096 computational control steps, zero new/new[] allocations.\n";
        if (argc==3) { require(std::string(argv[2])=="--benchmark","Unknown argument."); benchmark(model,cases); }
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
