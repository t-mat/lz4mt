#ifndef LZ4MT_COMPAT_H
#define LZ4MT_COMPAT_H

namespace Lz4Mt {

unsigned getHardwareConcurrency();

struct launch {
#if defined(_MSC_VER)
	typedef std::launch::launch Type;
#else
	typedef std::launch Type;
#endif
	static const Type deferred;
	static const Type async;
};

}

#endif
