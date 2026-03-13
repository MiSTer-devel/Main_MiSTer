#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H

#include <stdint.h>

// macro to tell if a new vertical refresh has happened since the last time we checked
// requires a local uint64_t to track frame counter
#define FRAME_TICK(last) \
    ((global_frame_counter != (last)) ? ((last) = global_frame_counter, 1) : 0)

void frame_timer();

// global
extern uint64_t global_frame_counter; // used by FRAME_TICK()
extern bool fpga_vsync_timer;         // does this core expose the frame counter directly?
                                      // exposed globally in case timerfd isn't accurate enough for some use cases

#endif
