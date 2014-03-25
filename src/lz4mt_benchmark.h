#ifndef LZ4MT_BENCHMARK_H
#define LZ4MT_BENCHMARK_H

#include <string>
#include <vector>
#include <functional>
#include "lz4mt.h"

namespace Lz4Mt {

class Benchmark {
public:
	Benchmark();
	~Benchmark();
	int measure(Lz4MtContext& ctx, const Lz4MtStreamDescriptor& sd);

	bool						enable;
	bool						pause;
	int							nIter;
	std::vector<std::string>	files;
	std::function<bool (Lz4MtContext* ctx, const std::string& filename)> openIstream;
	std::function<void (Lz4MtContext* ctx)> closeIstream;
	std::function<uint64_t (const std::string& filename)> getFilesize;
};

}
#endif
