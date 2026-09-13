#pragma once
#include "MagneticModel.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace model_test {
using namespace sixmag::control;
struct Case { Vector2 r; MagnetAngles theta; std::array<double,24> expected; };
inline std::array<double,24> flatten(const ModelEvaluation& out)
{
    std::array<double,24> a{};
    std::size_t i=0;
    for (double v:out.acceleration) a[i++]=v;
    for (const auto& row:out.positionJacobian) for (double v:row) a[i++]=v;
    for (const auto& row:out.angleJacobian) for (double v:row) a[i++]=v;
    for (double v:out.scaledField) a[i++]=v;
    for (const auto& row:out.scaledFieldJacobian) for (double v:row) a[i++]=v;
    return a;
}
inline std::vector<Case> readCases(const char* filename)
{
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot read MATLAB reference fixtures.");
    std::vector<Case> cases;
    std::string line;
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line);
        std::string word;
        std::array<double,32> values{};
        std::size_t i=0;
        while (std::getline(row,word,',')) {
            if (i==values.size()) throw std::runtime_error("Too many fixture columns.");
            std::size_t used=0;
            const double v=std::stod(word,&used);
            if (!std::isfinite(v) || word.find_first_not_of(" \r\t",used)!=std::string::npos)
                throw std::runtime_error("Invalid fixture number.");
            values[i++]=v;
        }
        if (i!=values.size()) throw std::runtime_error("Incomplete fixture row.");
        Case c{};
        for (i=0;i<2;++i) c.r[i]=values[i];
        for (i=0;i<6;++i) c.theta[i]=values[2+i];
        for (i=0;i<24;++i) c.expected[i]=values[8+i];
        cases.push_back(c);
    }
    if (cases.size()!=563) throw std::runtime_error("Expected 563 reference cases.");
    return cases;
}
} // namespace model_test
