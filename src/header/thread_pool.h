#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

// A small persistent thread pool. Worker threads are created once and parked on a
// condition variable between frames, so per-frame parallel work doesn't pay
// OS thread creation/teardown cost (which is the source of large, unpredictable
// lag spikes when threads are spawned every frame).
class ThreadPool {
public:
    explicit ThreadPool(int numThreads) : numThreads(numThreads), ranges(numThreads) {
        for(int t = 0; t < numThreads; t++) {
            workers.emplace_back([this, t]() { workerLoop(t); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            shutdown = true;
            generation++;
        }
        cvStart.notify_all();
        for(auto& w : workers) w.join();
    }

    // Splits [0, count) into numThreads contiguous chunks, runs job(start,end) for
    // each chunk on the pool, and blocks until all chunks have completed.
    void runParallel(int count, const std::function<void(int,int)>& jobFunc) {
        if(count <= 0) return;

        int chunkSize = (count + numThreads - 1) / numThreads;
        for(int t = 0; t < numThreads; t++) {
            int start = t * chunkSize;
            int end = start + chunkSize;
            if(end > count) end = count;
            if(start > count) start = count;
            ranges[t] = {start, end};
        }

        {
            std::unique_lock<std::mutex> lock(mtx);
            job = jobFunc;
            remaining = numThreads;
            generation++;
            cvStart.notify_all();
            cvDone.wait(lock, [this]{ return remaining == 0; });
        }
    }

private:
    void workerLoop(int idx) {
        int lastGeneration = 0;
        while(true) {
            std::function<void(int,int)> jobCopy;
            int start, end;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cvStart.wait(lock, [&]{ return generation != lastGeneration; });
                lastGeneration = generation;
                if(shutdown) return;
                jobCopy = job;
                start = ranges[idx].first;
                end   = ranges[idx].second;
            }

            jobCopy(start, end);

            {
                std::unique_lock<std::mutex> lock(mtx);
                remaining--;
                if(remaining == 0) cvDone.notify_one();
            }
        }
    }

    int numThreads;
    std::vector<std::thread> workers;
    std::vector<std::pair<int,int>> ranges;
    std::function<void(int,int)> job;

    std::mutex mtx;
    std::condition_variable cvStart, cvDone;
    int generation = 0;
    int remaining = 0;
    bool shutdown = false;
};

#endif // THREAD_POOL_H
