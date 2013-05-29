#ifndef CLOCK_H
#define CLOCK_H

#include <chrono>
#include <ratio>


// http://stackoverflow.com/a/15755865/2132223
//
// example:
//
//     int main() {
//         const auto t0 = Clock::now();
//         do_something();
//         const auto t1 = Clock::now();
//         const auto dSeconds = (t1 - t0).count();
//         std::cout << dSeconds;
//     }
//
#if defined(_WIN32)
struct Clock {
	typedef double                             rep;
	typedef std::ratio<1>                      period;
	typedef std::chrono::duration<rep, period> duration;
	typedef std::chrono::time_point<Clock>     time_point;

	static const bool is_steady = false;

	static time_point now() {
		static const auto frequency = init_frequency();
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		const auto r = static_cast<rep>(t.QuadPart);
		return time_point(duration(r/frequency));
	}

private:
	static long long init_frequency() {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return f.QuadPart;
	}
};
#else
typedef std::chrono::high_resolution_clock Clock;
#endif


#endif
