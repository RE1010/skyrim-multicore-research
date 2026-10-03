#pragma once
#include <algorithm>
#include <cmath>
namespace uncap_policy {
struct Times {float normal,complex;bool valid;};
inline Times scale(float unscaled,float original_normal,float original_complex) noexcept {
    if(!std::isfinite(unscaled) || unscaled<0 || !std::isfinite(original_normal) || !std::isfinite(original_complex)
        || original_normal<=0 || original_complex<=0) return {original_normal,original_complex,false};
    if(unscaled==0) return {original_normal,original_complex,true}; // paused timer, retain original physics budgets
    if(!std::isnormal(unscaled)) return {original_normal,original_complex,false};
    // This bounds physics budgets, not rendering FPS. Retain headroom above
    // the original 1ms minimum step; tiny render deltas must not shrink it away.
    const auto interval=std::clamp(unscaled,1.0f/240.0f,1.0f/60.0f);
    const auto complex=static_cast<float>(1.0/(std::max)(1.0/static_cast<double>(interval)-30.0,30.0));
    if(!std::isnormal(interval) || !std::isnormal(complex)) return {original_normal,original_complex,false};
    return {(std::min)(interval,original_normal),(std::min)(complex,original_complex),true};
}
}
