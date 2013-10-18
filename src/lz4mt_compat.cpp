#include <cassert>
#include <future>

#if defined(__GLIBC__)
#include <sys/sysinfo.h> // get_nprocs()
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#include "lz4mt_compat.h"


#ifndef GCC_VERSION
#  if defined(__GNUC__) && defined(__GNUC_MINOR__) && !defined(__clang__)
#    define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  else
#    define GCC_VERSION 0
#  endif
#endif

#if (GCC_VERSION <= 406) && (GCC_VERSION > 0)
#  define LAUNCH_SINGLE_THREAD std::launch::sync
#else
#  define LAUNCH_SINGLE_THREAD std::launch::deferred
#endif // defined(__GNUC__)

const decltype(Lz4Mt::launch::deferred) Lz4Mt::launch::deferred = LAUNCH_SINGLE_THREAD;
const decltype(Lz4Mt::launch::async)    Lz4Mt::launch::async    = std::launch::async;


unsigned Lz4Mt::getHardwareConcurrency() {
	{
		const auto c = std::thread::hardware_concurrency();
		if(c) {
			return static_cast<unsigned>(c);
		}
	}

	// following code is borrowed from boost-1.53.0/libs/thread/src/pthread/thread.cpp
#if defined(__APPLE__) || defined(__FreeBSD__)
	{
		int c = 0;
		size_t size = sizeof(c);
		if(0 == sysctlbyname("hw.ncpu", &c, &size, NULL, 0)) {
			return static_cast<unsigned>(c);
		}
	}
#endif

#if defined(__GLIBC__)
	{
		auto c = get_nprocs();
		if(c) {
			return static_cast<unsigned>(c);
		}
	}
#endif
	assert(0);
	return 8;
}
