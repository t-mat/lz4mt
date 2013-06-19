#ifndef LZ4MT_THREADPOOL_H
#define LZ4MT_THREADPOOL_H

namespace Lz4Mt {

class ThreadPool {
public:
	ThreadPool(size_t nThread = 0);
	~ThreadPool();
	void joinAll();

	template<class F>
	void enqueue(F&& f) {
		{
			Lock lock(mut);
			tasks.push(std::move(f));
		}
		cond.notify_one();
	}

private:
	typedef std::unique_lock<std::mutex> Lock;
	typedef std::function<void(int)> Task;

	std::atomic<bool> stop;
	mutable std::mutex mut;
	std::condition_variable cond;

	std::queue<Task> tasks;
	std::vector<std::thread> threads;
};

} // namespace Lz4Mt

#endif // LZ4MT_THREADPOOL_H
