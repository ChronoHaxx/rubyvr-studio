// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rubyvr::dev {
// Guest VBlanks per wall-clock second; independent of display cadence. The
// caller uses a monotonic clock and resets on pause/load/speed changes.
class FrameRate {
public:
    void reset(){*this={};}
    void observe(uint64_t frame,double seconds) {
        if(!started_ || frame<last_ || seconds<begin_) {
            started_=true;first_=last_=frame;begin_=seconds;fps_=0;return;
        }
        last_=frame;
        if(seconds-begin_>=0.5) {
            fps_=double(frame-first_)/(seconds-begin_);
            first_=frame;begin_=seconds;
        }
    }
    double speed() const {return fps_/59.727500569606;}
private:
    bool started_=false;
    uint64_t first_=0,last_=0;
    double begin_=0,fps_=0;
};
}
