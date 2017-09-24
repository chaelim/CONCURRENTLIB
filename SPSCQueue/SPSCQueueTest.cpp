/**
*
*   SPSCQueue.CPP
*   Unbounded Single Producer Single Consumer queue test
*
*   NOTE:
*      - On multi core machine, this test program best performed when using only two cores.
*           (121M operations/second on Intel i7-4870HQ)
*      - To set CPU affinity you can use "start /b /affinity 3 SPSCQueueTest.exe" command.
*
*   Written by CS Lim (10/04/2010)
* 
*   Commandline Compile:
*      Release build: cl.exe /std:c++latest /GS- /Zi /MT /O2 /Ox /EHsc /DNDEBUG /D _WIN32 SPSCQueueTest.cpp
*      Debug build: cl.exe /std:c++latest /MTd /Od /EHsc /D_DEBUG SPSCQueueTest.cpp
*
**/

#pragma once

#include "SPSCQueue.h"
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <assert.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace std::chrono_literals;

static std::atomic<bool> s_stopTesting{false};
static std::atomic<int> s_runTestThreads{0};
static std::mutex g_mutex;
static std::condition_variable g_cv;
static volatile bool g_ready = false;

static TSPSCQueue<unsigned> s_spscQueue;

//===========================================================================
// Wait until main thread signal ready.
//===========================================================================
void wait()
{
    // Wait until main thread sends ready signal
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, []{return g_ready;});
}

//===========================================================================
// RunThreads
//===========================================================================
void RunThreads()
{
    std::thread producer([&]() {
        // Wait for start
        std::atomic_fetch_add(&s_runTestThreads, 1);
        wait();

        unsigned i = 0;
        while (!s_stopTesting) {
            s_spscQueue.Enqueue(i++);
            //Sleep(rand() % 10);
        }

        printf("Producer Total Enqueue: %u\n", i);
    });

    std::thread consumer([&]() {
        // Wait for start
        std::atomic_fetch_add(&s_runTestThreads, 1);
        wait();

        unsigned i = 0;
        while (!s_stopTesting) {
            unsigned j;
            if (!s_spscQueue.Dequeue(j))
                continue;

            assert(i == j);
            i++;
            //Sleep(rand() % 1000);
        }
        
        printf("Consumer Total Dequeue: %u\n", i);
    });

#if defined(_WIN32)
    // Set higher priority for more accurate benchmark
    SetThreadPriority(producer.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
    SetThreadPriority(consumer.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    // Wait until all threads are ready
    while (s_runTestThreads != 2)
        std::this_thread::sleep_for(10ms);

    g_ready = true;
    g_cv.notify_all();

    // Run test for specified test time
    std::this_thread::sleep_for(20s);

    // Stop testing
    s_stopTesting.store(true, std::memory_order_relaxed);

    producer.join();
    consumer.join();
}

void SignalHandler(int signal)
{
    if (signal == SIGINT)
    {
        s_stopTesting.store(true, std::memory_order_relaxed);
    }
    
    exit(signal);
}

//===========================================================================
// main
//===========================================================================
int main()
{
    std::signal(SIGINT, SignalHandler);

    RunThreads();

    printf("Succeeded\n");
}