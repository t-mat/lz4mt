#include <cassert>
#include <atomic>
#include <future>
#include <mutex>
#include <queue>
#include <vector>
#include "lz4mt_threadpool.h"
#include "lz4mt_compat.h"

namespace Lz4Mt {

// https://github.com/progschj/ThreadPool

ThreadPool::ThreadPool(size_t nThread)
	: stop()
	, mut()
	, cond()
	, tasks()
	, threads()
{
	stop.store(false);

	if(0 == nThread) {
		nThread = Lz4Mt::getHardwareConcurrency();
		assert(nThread != 0);
		if(0 == nThread) {
			nThread = 1;
		}
	}

	for(int i = 0; i < static_cast<int>(nThread); ++i) {
		threads.push_back(std::thread(
			[this, i] {
				for(;;) {
					Lock lock(mut);
					while(!stop.load() && tasks.empty()) {
						cond.wait(lock);
					}
					if(stop.load() && tasks.empty()) {
						break;
					}
					const auto t = std::move(tasks.front());
					tasks.pop();
					lock.unlock();
					t(i);
				}
			}
		));
	}
}


ThreadPool::~ThreadPool() {
	joinAll();
}


void ThreadPool::joinAll() {
	{
		Lock lock(mut);
		stop.store(true);
	}
	cond.notify_all();

	for(auto& e : threads) {
		if(e.joinable()) {
			e.join();
		}
	}
}

} // namespace Lz4Mt
