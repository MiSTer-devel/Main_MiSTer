#include "offload.h"
#include "profiling.h"
#include <pthread.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

static constexpr uint32_t QUEUE_SIZE = 8;

static pthread_t s_thread_handle;
static pthread_cond_t s_cond_work, s_cond_available;
static pthread_mutex_t s_queue_lock;

struct Work
{
	std::function<void()> handler;
};

static Work s_queue[QUEUE_SIZE];
static uint32_t s_queue_head, s_queue_tail;
static bool s_quit;

static void *worker_thread(void *)
{
	while (true)
	{
		Work *current_work = nullptr;
		// Wait for work
		pthread_mutex_lock(&s_queue_lock);
		if (s_queue_head == s_queue_tail)
		{
			// queue empty and quit flag set, exit
			if (s_quit)
			{
				pthread_mutex_unlock(&s_queue_lock);
				break;
			}

			// wait for work signal
			pthread_cond_wait(&s_cond_work, &s_queue_lock);
			
			// quit flag was set and queue still empty, quit
			if (s_quit && (s_queue_head == s_queue_tail))
			{
				pthread_mutex_unlock(&s_queue_lock);
				break;
			}
		}

		// get work
		current_work = &s_queue[s_queue_tail % QUEUE_SIZE];
		pthread_mutex_unlock(&s_queue_lock);

		// execute
		current_work->handler();
		current_work->handler = nullptr;

		// lock and move tail forward
		pthread_mutex_lock(&s_queue_lock);
		s_queue_tail++;
		pthread_cond_signal(&s_cond_available);
		pthread_mutex_unlock(&s_queue_lock);
	}
	return (void *)0;
}

void offload_start()
{
	pthread_cond_init(&s_cond_available, nullptr);
	pthread_cond_init(&s_cond_work, nullptr);
	pthread_mutex_init(&s_queue_lock, nullptr);

	s_queue_head = s_queue_tail = 0;
	s_quit = false;

	pthread_attr_t attr;

	pthread_attr_init(&attr);

	// Set affinity to core #0 since main runs on core #1
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	pthread_attr_setaffinity_np(&attr, sizeof(set), &set);

	pthread_create(&s_thread_handle, &attr, worker_thread, nullptr);
}

void offload_stop()
{
	pthread_mutex_lock(&s_queue_lock);

	s_quit = true;
	pthread_cond_signal(&s_cond_work);

	pthread_mutex_unlock(&s_queue_lock);

	printf("Waiting for offloaded work to finish...");
	pthread_join(s_thread_handle, nullptr);
	printf("Done\n");
}

void offload_add_work(std::function<void()> handler)
{
	PROFILE_FUNCTION();

	pthread_mutex_lock(&s_queue_lock);

	if ((s_queue_head - s_queue_tail) == QUEUE_SIZE)
	{
		pthread_cond_wait(&s_cond_available, &s_queue_lock);
	}

	Work *work = &s_queue[s_queue_head % QUEUE_SIZE];
	work->handler = handler;

	s_queue_head++;

	pthread_cond_signal(&s_cond_work);

	pthread_mutex_unlock(&s_queue_lock);
}