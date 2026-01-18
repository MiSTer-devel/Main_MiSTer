#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include <unistd.h>
#include <math.h>

#include "user_io.h"
#include "autofire_timer.h"
#include "video.h"

/************* AUTOFIRE TIMER STUFF ********************/
// autofire timing
// attempt to use actual refresh rate of core to synchronize
// or even better see if the core offers its own frame counter
// autofire as frame-on/frame-off

// global
uint64_t global_frame_counter = 0;

extern VideoInfo *pcurrent_video_info; // from video.cpp
static bool timer_started = false;
static int vtimerfd = -1;
static uint64_t vtimer_start_ns;
static bool fpga_vsync_timer = false; // does this core expose the frame counter directly?

#define MINHZ FIFTYHERTZ
#define MAXHZ SEVENTYFIVEHERTZ
#define DEFHZ SIXTYHERTZ

// clamp calculated vrefresh to 50-75hz to avoid obviously garbage values
// (75hz because somebody is bound to demand frame synced autofire on wonderswan)
static inline uint32_t get_vtime() {
	uint32_t current_vtime = pcurrent_video_info->vtime;
	if (current_vtime <= MINHZ && current_vtime >= MAXHZ) {
		return current_vtime;
	}
	else {
		return DEFHZ;
	}
}

// return true if a core's vtime has changed
// this might happen if resolution changes or some cores
// allow the user to adjust vrefresh for display compatibility
static inline bool vtime_changed()
{
	static uint32_t prev_vtime;
	uint32_t current_vtime = get_vtime();
	if (prev_vtime != current_vtime) {
		if (vtimerfd >= 0) {
			close(vtimerfd);	// recycle timerfd
			vtimerfd = -1;
		}
		prev_vtime = current_vtime;
		return true;
	}
	return false;
}

// initialize timerfd based timer
// return 1 on failure
int start_vtimer(uint64_t interval_ns) {
	vtimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (vtimerfd < 0) {
        perror("timerfd_create");
        return 1;
    };

    // get current time to start absolute time
	// not to be confused with absolute batman
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
        perror("timerfd_settime");
		printf("interval_ns: %llu\n", interval_ns);
        return 1;
    }
	float hz = 1e9f / interval_ns;
    printf("%.2fhz timer started.\n", hz);
	return 0;
}

// attempt to start timerfd based timer
// returns false on failure or if vrefresh appears to be 0
static inline bool init_af_timer(uint32_t interval)
{
	printf("autofire_timer(): core does not offer framecounter. using timerfd.\n");
	uint64_t interval_ns = interval * 10ull;
		if (!timer_started && interval_ns) {
			if (start_vtimer(interval_ns) == 0)
				return true;
			else
				return false;
		}
	return false;
}

// check if timer has fired yet or not.
// return nanoseconds since timer expired
// return zero otherwise
static uint64_t check_vtimer(uint64_t interval_ns) {
	uint64_t ns;
	uint64_t expirations;
	struct timespec now;

	struct pollfd pfd = { vtimerfd, POLLIN, 0 };

	if (poll(&pfd, 1, 0) <= 0)
		return 0; // timer not ready

	read(vtimerfd, &expirations, sizeof(expirations));

	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
	uint64_t expected_ns = vtimer_start_ns + expirations * interval_ns;
	ns = now_ns - expected_ns;
	
	return ns;
}

// call this periodically, right now we do it at the start of input_poll() in input.cpp
void autofire_timer() {
	// if core offers its own framecounter skip all the timerfd nonsense
	uint32_t frcnt = spi_uio_cmd(UIO_GET_FR_CNT);
	if (frcnt & 0x100) {
		if (!fpga_vsync_timer) printf("autofire_timer(): core offers framecounter.\n");
		fpga_vsync_timer = true;
		global_frame_counter = frcnt & 0xFF;
	}
	else { // fallback to timerfd
		if (vtime_changed())
			timer_started = false; // restart timers if vrefresh has changed;
		uint32_t vtime = get_vtime();
		if (!timer_started)
        	timer_started = init_af_timer(vtime);
		if (timer_started && check_vtimer(vtime))
			global_frame_counter++;
	}
}