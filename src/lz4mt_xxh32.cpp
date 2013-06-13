#include <cassert>
#include <atomic>
#include <future>
#include <mutex>
#include <vector>
#include "xxhash.h"
#include "lz4mt_xxh32.h"

namespace {
typedef std::unique_lock<std::mutex> Lock;
} // anonymous namespace


namespace Lz4Mt {

Xxh32::Xxh32(uint32_t seed)
	: mut()
	, st(new char[XXH32_sizeofState()])
{
	Lock lock(mut);
	XXH32_resetState(st.get(), seed);
}


Xxh32::Xxh32(const void* input, int len, uint32_t seed)
	: mut()
	, st(new char[XXH32_sizeofState()])
{
	Lock lock(mut);
	XXH32_resetState(st.get(), seed);
	XXH32_update(st.get(), input, len);
}


Xxh32::~Xxh32() {
	Lock lock(mut); // wait for release
	st.reset();
}


bool Xxh32::update(const void* input, int len) {
	Lock lock(mut);
	if(st) {
		return XXH_OK == XXH32_update(st.get(), input, len);
	} else {
		return false;
	}
}


uint32_t Xxh32::digest() {
	Lock lock(mut);
	if(st) {
		return XXH32_intermediateDigest(st.get());
	} else {
		return 0;
	}
}

} // namespace Lz4Mt
