// Exercise the ESP32-S3 int8 CSI path, independently of the UI demo data.
#include "../src/sense_cyd/csi_motion_model.h"
#include <cmath>
#include <cstdio>

int main() {
    CsiMotionModel model;
    model.reset(0,15000,1000);
    uint32_t accepted=0;
    auto feed=[&](uint32_t from,uint32_t to,bool moving) {
        for(uint32_t ms=from;ms<=to;ms+=50) {
            int8_t iq[128];
            for(int k=0;k<64;++k) {
                const float phase=0.2f*k+(moving ? 0.8f*sinf(ms*0.007f+k*0.7f):0);
                const float amp=35+(moving ? 18*sinf(ms*0.011f+k):0);
                iq[2*k]=(int8_t)(amp*sinf(phase));
                iq[2*k+1]=(int8_t)(amp*cosf(phase));
            }
            model.addSample(iq,sizeof iq,-48,ms,false,CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
            model.tick(ms,++accepted,0);
        }
    };
    feed(50,20000,false);
    const bool still=model.calibrated() && model.motionScore()<12 && model.reliability()>=25;
    feed(20050,30000,true);
    const bool motion=model.motionScore()>=28 && model.state()==CsiMotionModel::State::MOTION;
    feed(30050,40000,false);
    const bool settles=model.motionScore()<12;
    model.tick(44000,accepted,0);
    const bool lost=model.state()==CsiMotionModel::State::INSUFFICIENT_SAMPLES;
    // Receiving packets with unusable CSI must not report a healthy detector.
    int8_t blank[128]={};
    for(uint32_t ms=44050;ms<=48000;ms+=50) {
        model.addSample(blank,sizeof blank,-48,ms,false,CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
        model.tick(ms,++accepted,0);
    }
    const bool rejectsBlank=model.samplesPerSecond()==0 && model.reliability()<25;
    for(auto& b:blank)b=35;
    for(uint32_t ms=48050;ms<=52000;ms+=50) {
        model.addSample(blank,(ms/50)%2 ? 128:32,-48,ms,false,CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
        model.tick(ms,++accepted,0);
    }
    const bool rejectsStale=model.samplesPerSecond()==20 && model.reliability()<25 && model.state()==CsiMotionModel::State::INSUFFICIENT_SAMPLES;
    // Uniform fading is real channel motion too. The old unit-mean
    // normalization erased it completely, leaving every feature at zero.
    CsiMotionModel fading;
    fading.reset(0,15000,1000); uint32_t count=0;
    bool noisy=false;
    auto fade=[&](uint32_t from,uint32_t to,bool moving,bool gainOnly) {
        for(uint32_t ms=from;ms<=to;ms+=50) {
            int8_t iq[128]={};
            const int strength=35+(moving ? (int)(15*sinf(ms*.013f)):0);
            for(int k=0;k<64;++k)iq[k*2+1]=strength;
            const int8_t rssi=-48+(noisy ? (int)(1.4f*sinf(ms*.019f)):0)+(moving && !gainOnly ? (int)(6*sinf(ms*.013f)):0);
            fading.addSample(iq,sizeof iq,rssi,ms,false,CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
            fading.tick(ms,++count,0);
        }
    };
    fade(50,20000,false,false);
    fade(20050,26000,true,true);
    const bool ignoresGain=fading.motionScore()<12;
    fade(26050,36000,true,false);
    const bool detectsFade=fading.motionScore()>=28 && fading.variation()>0;
    fade(36050,46000,false,false);
    const bool fadeSettles=fading.motionScore()<12;
    fading.reset(0,15000,1000);count=0;noisy=true;
    fade(50,20000,false,false);
    const bool learnsNoise=fading.motionScore()<12;
    fade(20050,30000,true,false);
    const bool detectsAboveNoise=fading.motionScore()>=28;
    fade(30050,40000,false,false);
    const bool noiseSettles=fading.motionScore()<12;
    printf("CSI int8: calibration/still=%d motion=%d settles=%d signal-loss=%d\n",still,motion,settles,lost);
    printf("CSI health: zero payloads=%d stale feature windows=%d\n",rejectsBlank,rejectsStale);
    printf("CSI strength: ignores receiver gain=%d detects fading=%d settles=%d\n",ignoresGain,detectsFade,fadeSettles);
    printf("CSI noise: learns background=%d detects movement=%d settles=%d\n",learnsNoise,detectsAboveNoise,noiseSettles);
    return still && motion && settles && lost && rejectsBlank && rejectsStale && ignoresGain && detectsFade && fadeSettles && learnsNoise && detectsAboveNoise && noiseSettles ? 0:1;
}
