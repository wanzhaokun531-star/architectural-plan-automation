#pragma once

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace bcpproduct {
inline std::wstring planOpeningCode(const wchar_t* prefix,double widthMM,double heightMM) {
    if (!prefix || !*prefix || !std::isfinite(widthMM) || !std::isfinite(heightMM) ||
        widthMM <= 0 || heightMM <= 0) return {};
    const auto equal = [](double a,double b) { return std::abs(a-b)<=1e-5; };
    if (equal(widthMM,900.) && equal(heightMM,2350.)) return std::wstring(prefix)+L"09235";
    const double width = std::round(widthMM/100), height = std::round(heightMM/100);
    if (width<1 || width>99 || height<1 || height>99 || !equal(width*100,widthMM) || !equal(height*100,heightMM))
        return {};
    std::wostringstream value;
    value<<prefix<<std::setfill(L'0')<<std::setw(2)<<int(width)<<std::setw(2)<<int(height);
    return value.str();
}
}
