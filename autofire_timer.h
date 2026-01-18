#define SIXTYHERTZ  1668335 // fallback if vtime doesn't work; actually 59.94hz
#define FIFTYHERTZ 2000000 // lowest refresh rate we consider valid 50hz
#define SEVENTYFIVEHERTZ 1326260 // highest refresh rate we consider valid 75.4hz

// macro to tell if a new vertical refresh has happened since the last time we checked
// requires a local uint64_t to track frame counter
#define FRAME_TICK(last) \
    ((global_frame_counter != (last)) ? ((last) = global_frame_counter, 1) : 0)

void autofire_timer();

// global
extern uint64_t global_frame_counter;