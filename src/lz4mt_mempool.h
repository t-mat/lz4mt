#ifndef LZ4MT_MEMPOOL_H
#define LZ4MT_MEMPOOL_H

#include <stack>

namespace Lz4Mt {

class MemPool {
public:
	class Buffer;

	MemPool(size_t elementSize, size_t elementCount);
	~MemPool();
	Buffer* alloc();

	class Buffer {
		typedef std::function<void(void)> Callback;

	public:
		Buffer(char* ptr, size_t contentSize, Callback callback);
		~Buffer();
		char* data() const;
		size_t size() const;
		void resize(size_t contentSize);

	private:
		Buffer(const Buffer&);
		const Buffer& operator=(const Buffer&);

		char* ptr;
		size_t contentSize;
		Callback callback;
	};

private:
	typedef std::vector<char> Element;

	std::atomic<bool> stop;
	mutable std::mutex mut;
	std::condition_variable cond;

	std::stack<int> freeIndexStack;
	std::vector<Element> elements;
};

} // namespace Lz4Mt

#endif // LZ4MT_MEMPOOL_H
