#include <cassert>
#include <atomic>
#include <future>
#include <mutex>
#include <vector>
#include "lz4mt_mempool.h"

namespace {
typedef std::unique_lock<std::mutex> Lock;
} // anonymous namespace


namespace Lz4Mt {

MemPool::MemPool(size_t elementSize, size_t elementCount)
	: stop(false)
	, mut()
	, cond()
	, freeIndexStack()
	, elements()
{
	Lock lock(mut);
	elements.reserve(elementCount);
	for(size_t i = 0; i < elementCount; ++i) {
		elements.emplace_back(elementSize);
		freeIndexStack.push(static_cast<int>(i));
	}
}


MemPool::~MemPool() {
	{
		Lock lock(mut);
		stop = true;
	}
	cond.notify_all();
}


MemPool::Buffer* MemPool::alloc() {
	for(;;) {
		Lock lock(mut);
		if(!stop && freeIndexStack.empty()) {
			cond.wait(lock);
		}
		if(stop) {
			return new Buffer(nullptr, 0, []{});
		}
		if(! freeIndexStack.empty()) {
			const auto i = freeIndexStack.top();
			freeIndexStack.pop();
			auto& e = elements[i];
			return new Buffer(e.data(), e.size()
				, [this, &e, i]() {
					Lock lock(mut);
					freeIndexStack.push(i);
					cond.notify_one();
				}
			);
		}
	}
}


MemPool::Buffer::Buffer(char* ptr, size_t contentSize, Callback callback)
	: ptr(ptr)
	, contentSize(contentSize)
	, callback(callback)
{}

MemPool::Buffer::~Buffer() {
	callback();
}

char* MemPool::Buffer::data() const {
	return ptr;
}

size_t MemPool::Buffer::size() const {
	return contentSize;
}

void MemPool::Buffer::resize(size_t contentSize) {
	this->contentSize = contentSize;
}


} // namespace Lz4Mt
