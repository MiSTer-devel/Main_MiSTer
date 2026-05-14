#include "scheduler.h"
#include <stdio.h>
#include "libco.h"
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "frame_timer.h"
#include "fpga_io.h"
#include "osd.h"
#include "profiling.h"
#include "cfg.h"
#include "hardware.h"
#include "hdmi_cec.h"

static cothread_t co_scheduler = nullptr;
static cothread_t co_poll = nullptr;
static cothread_t co_ui = nullptr;
static cothread_t co_last = nullptr;
static unsigned long cec_retry = 0;
static bool cec_init_failed_logged = false;
static bool scheduler_ui_ran_once = false;

static void scheduler_wait_fpga_ready(void)
{
	while (!is_fpga_ready(1))
	{
		fpga_wait_to_reset();
	}
}

static void scheduler_co_poll(void)
{
	for (;;)
	{
		scheduler_wait_fpga_ready();

		{
			SPIKE_SCOPE("co_poll", 1000);
			user_io_poll();
			frame_timer();
			input_poll(0);
		}

		if (cfg.hdmi_cec)
		{
			if (scheduler_ui_ran_once && !cec_is_enabled() && CheckTimer(cec_retry))
			{
				if (!cec_init(true))
				{
					if (cfg.debug && !cec_init_failed_logged) printf("CEC: init failed\n");
					cec_init_failed_logged = true;
					cec_retry = GetTimer(3000);
				}
				else
				{
					cec_init_failed_logged = false;
					cec_retry = 0;
				}
			}

			if (cec_is_enabled()) cec_poll();
		}
		else
		{
			if (cec_is_enabled()) cec_deinit();
			cec_retry = 0;
			cec_init_failed_logged = false;
		}

		scheduler_yield();
	}
}

static void scheduler_co_ui(void)
{
	for (;;)
	{
		scheduler_ui_ran_once = true;

		{
			SPIKE_SCOPE("co_ui", 1000);
			HandleUI();
			OsdUpdate();
		}

		scheduler_yield();
	}
}

static void scheduler_schedule(void)
{
	if (co_last == co_poll)
	{
		co_last = co_ui;
		co_switch(co_ui);
	}
	else
	{
		co_last = co_poll;
		co_switch(co_poll);
	}
}

void scheduler_init(void)
{
	const unsigned int co_stack_size = 262144 * sizeof(void*);

	co_poll = co_create(co_stack_size, scheduler_co_poll);
	co_ui = co_create(co_stack_size, scheduler_co_ui);
}

void scheduler_run(void)
{
	co_scheduler = co_active();

	for (;;)
	{
		scheduler_schedule();
	}

	co_delete(co_ui);
	co_delete(co_poll);
	co_delete(co_scheduler);
}

void scheduler_yield(void)
{
	co_switch(co_scheduler);
}
