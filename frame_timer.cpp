#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include <unistd.h>

#include "user_io.h"
#include "frame_timer.h"
#include "video.h"

// frame timer used by autofire; call frame_timer() periodically and use FRAME_TICK().
// prefers the core's frame counter, otherwise falls back to timerfd.

#define SIXTYHERTZ  1668335      // fallback if vtime doesn't work; actually 59.94hz
#define FIFTYHERTZ 2000000       // lowest refresh rate we consider valid 50hz
#define SEVENTYFIVEHERTZ 1326260 // highest refresh rate we consider valid 75.4hz

uint64_t global_frame_counter = 0;

extern VideoInfo current_video_info; // from video.cpp
static bool timer_started = false;
static int vtimerfd = -1;

bool fpga_vsync_timer = false; // flag to indicate core provides frame counter.

// clamp vrefresh to 50-75Hz; otherwise use 60Hz.
static inline uint32_t get_vtime() {
	uint32_t current_vtime = current_video_info.vtime;
	// not a bug - a higher refresh rate in hz means a *smaller* vtime value in 10ns units.
	if (current_vtime <= FIFTYHERTZ && current_vtime >= SEVENTYFIVEHERTZ) {
		return current_vtime;
	}
	else {
		return SIXTYHERTZ;
	}
}

// return true if core vtime changes (resolution or user refresh adjustment).
static inline bool vtime_changed()
{
	static uint32_t prev_vtime;
	uint32_t current_vtime = get_vtime();
	if (prev_vtime != current_vtime) {
		if (vtimerfd >= 0) {
			close(vtimerfd);	// recycle timerfd
			vtimerfd = -1;
		}
		printf("frame_timer(): vtime change detected, restarting timer.\n");
		prev_vtime = current_vtime;
		return true;
	}
	return false;
}

// initialize timerfd-based timer; returns 1 on failure.
int start_vtimer(uint64_t interval_ns) {
	vtimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (vtimerfd < 0) {
        perror("timerfd_create");
        return 1;
    };

    // start timer at absolute CLOCK_MONOTONIC time.
	// not to be confused with absolute batman.
    struct timespec now_ts = {};
	struct itimerspec its = {};

    clock_gettime(CLOCK_MONOTONIC, &now_ts);
	
	uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
	uint64_t first_expiration_ns = now_ns + interval_ns;

    its.it_value.tv_sec  = first_expiration_ns / 1000000000ULL;
    its.it_value.tv_nsec = first_expiration_ns % 1000000000ULL;
	
	its.it_interval.tv_sec  = interval_ns / 1000000000ULL;
    its.it_interval.tv_nsec = interval_ns % 1000000000ULL;

    if (timerfd_settime(vtimerfd, TFD_TIMER_ABSTIME, &its, NULL) < 0) {
		close(vtimerfd);
		vtimerfd = -1;
		printf("frame_timer(): timerfd setup failed, will retry.\n");
        return 1;
    }
	float hz = 1e9f / interval_ns;
    printf("frame_timer(): core does not offer framecounter. using timerfd.\n");
	printf("%.2fhz timer started.\n", hz);
	return 0;
}

// attempt timerfd setup; returns false on failure or zero interval.
static inline bool init_frame_timer(uint32_t interval)
{
	uint64_t interval_ns = interval * 10ull;
		if (!timer_started && interval_ns) {
			if (start_vtimer(interval_ns) == 0)
				return true;
			else
				return false;
		}
	return false;
}

// return true if timer has expired, false otherwise.
static bool check_vtimer() {
	uint64_t expirations = 0;
	struct pollfd pfd = { vtimerfd, POLLIN, 0 };

	if (poll(&pfd, 1, 0) <= 0)
		return false; // timer not ready

	ssize_t n = read(vtimerfd, &expirations, sizeof(expirations));
	if (n != (ssize_t)sizeof(expirations) || expirations == 0)
		return false;

	return true;
}

// prefer core framecounter; fallback to timerfd with minor long-term drift risk.
// call periodically (e.g., start of input_poll()).
void frame_timer() {
	// if core offers its own framecounter skip all the timerfd nonsense
	uint32_t frcnt = spi_uio_cmd(UIO_GET_FR_CNT);
	if (frcnt & 0x100) {
		if (!fpga_vsync_timer) printf("frame_timer(): core offers framecounter.\n");
		fpga_vsync_timer = true;
		global_frame_counter = frcnt & 0xFF;
	}
	else { // fallback to timerfd
		if (vtime_changed())
			timer_started = false; // restart timers if vtime has changed;
		uint32_t vtime = get_vtime();
		if (!timer_started)
        	timer_started = init_frame_timer(vtime);
		if (timer_started && check_vtimer())
			global_frame_counter++;
	}
}
