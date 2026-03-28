#include "app/FuncbeatMelody.h"

#include <cmath>

// Source: https://dollchan.net/btb/res/3.html#5719
// Anonymous 98fa314d 25.03.25 Tue 03:53:18 № 5719 510 

// Source formula kept for reference / retuning:
//   let f=(iter=1)=>{
//     let p=Array(iter).fill(0),v=Array(iter).fill(0);
//     return(x,s=1,m=.5)=>{
//       let o=x;
//       for(let i=0;i<iter;i++){v[i]=(v[i]+((o-p[i])*s))*m;o=p[i]+=v[i];}
//       return o;
//     }
//   },sampleRate=16e3,TAU=Math.PI,lf=f(4);
//   return t=>{
//     let o=t*2%1<.005?1:0,
//     m=.6,freq=1700+Math.sin(t*1.4)*1500,theta=TAU*freq/sampleRate,
//     s=(-2*Math.cos(theta)*Math.sqrt(m)+m+1)/m;
//     o+=(Math.random()-.5)*.2;
//     o+=(t*33%1<.5+Math.sin(t)*.49?1:-1)*.2;
//     o-=(t*32.6%1<.5+Math.sin(t)*.499?1:-1)*.2;
//     o+=(t*330%1<.5?-1:1)*.02;
//     return Math.tanh(lf(o,s,m));
//   };

namespace {
constexpr float kOutputGain = 0.5F;
constexpr float kPi = 3.14159265358979323846F;
// m is constant (0.6); precompute derived values used every sample.
constexpr float kM = 0.6F;
constexpr float kSqrtM = 0.77459666924148337704F;  // sqrt(0.6)
constexpr float kInvM = 1.0F / kM;
// Matches MenuMusicPlayer::kSampleRate.
constexpr float kInvSampleRate = 1.0F / 16000.0F;
}  // namespace

void FuncbeatMelody::Reset() {
    for (int i = 0; i < 4; ++i) {
        filterP_[i] = 0.0F;
        filterV_[i] = 0.0F;
    }
    rngState_ = 2463534242U;
}

float FuncbeatMelody::Synthesize(float t, bool tense) {
    (void)tense;
    // Impulse train at 2 Hz, 0.5% duty cycle — drives the resonant filter.
    float o = std::fmod(t * 2.0F, 1.0F) < 0.005F ? 1.0F : 0.0F;

    // Sweeping resonant filter coefficients.
    // Note: "TAU" in the source equals Math.PI (not 2π) — preserved faithfully.
    // ω_filter = 7π/15: T_filter = 30/7 s (7 sweeps per grand cycle).
    const float freq = 1700.0F + std::sin(t * (7.0F * kPi / 15.0F)) * 1500.0F;
    const float theta = kPi * freq * kInvSampleRate;
    const float s = (-2.0F * std::cos(theta) * kSqrtM + kM + 1.0F) * kInvM;

    // Noise via xorshift32 — replaces Math.random(), avoids libc overhead.
    rngState_ ^= rngState_ << 13U;
    rngState_ ^= rngState_ >> 17U;
    rngState_ ^= rngState_ << 5U;
    o += (static_cast<float>(rngState_ >> 8) * (1.0F / 16777216.0F) - 0.5F) * 0.2F;

    // Detuned square oscillators with LFO-modulated pulse width.
    // ω_lfo = π/3: T_lfo = 6 s (5 LFO cycles per grand cycle).
    // Shared between both oscillators to save one trig call.
    const float sinT = std::sin(t * (kPi / 3.0F));
    o += (std::fmod(t * 33.0F, 1.0F) < 0.5F + sinT * 0.49F ? 1.0F : -1.0F) * 0.2F;
    o -= (std::fmod(t * 32.6F, 1.0F) < 0.5F + sinT * 0.499F ? 1.0F : -1.0F) * 0.2F;
    o += (std::fmod(t * 330.0F, 1.0F) < 0.5F ? -1.0F : 1.0F) * 0.02F;

    // 4-pole cascaded resonant filter (spring-damper chain), lf(o,s,m) in source.
    for (int i = 0; i < 4; ++i) {
        filterV_[i] = (filterV_[i] + (o - filterP_[i]) * s) * kM;
        filterP_[i] += filterV_[i];
        o = filterP_[i];
    }

    return std::tanh(o) * kOutputGain;
}
