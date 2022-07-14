#include "scheduler.h"
#include <stdio.h>
#include "libco.h"
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"
#include "osd.h"
#include "profiling.h"

static cothread_t co_scheduler = nullptr;
static cothread_t co_poll = nullptr;
static cothread_t co_ui = nullptr;
static cothread_t co_last = nullptr;

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
			input_poll(0);
		}

		scheduler_yield();
	}
}

static void scheduler_co_ui(void)
{
	for (;;)
	{
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
