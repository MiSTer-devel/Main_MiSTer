#ifndef OFFLOAD_H
#define OFFLOAD_H

#include <stddef.h>
#include <functional>

void offload_start();
void offload_stop();

void offload_add_work(std::function<void()> work);

// Non-blocking: returns false instead of waiting when the queue is full.
bool offload_try_add_work(std::function<void()> work);

#endif