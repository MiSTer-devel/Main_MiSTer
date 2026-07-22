#ifndef OFFLOAD_H
#define OFFLOAD_H

#include <stddef.h>
#include <functional>

void offload_start();
void offload_stop();

void offload_add_work(std::function<void()> work);

// Non-blocking variant: returns false and enqueues nothing if the queue is
// full, instead of waiting for a slot. For callers on the main thread that
// have a synchronous fallback and must never block -- offload_add_work()
// blocks on a full queue, which would stall the very loop the offload is
// meant to keep moving.
bool offload_try_add_work(std::function<void()> work);

#endif