#include <cassert>
#include <cstdio>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "xxhash.h"
#include "lz4mt.h"
#include "lz4mt_benchmark.h"

namespace {

typedef std::chrono::high_resolution_clock Clock;
typedef Clock::time_point TimePoint;

TimePoint getTime() {
	return Clock::now();
}

TimePoint getSyncTime() {
	auto t0 = getTime();
	for(;;) {
		auto t = getTime();
		if(t0 != t) {
			return t;
		}
	}
}

double getTimeSpan(const TimePoint& tStart, const TimePoint& tEnd) {
	using namespace std::chrono;
	auto dt = duration_cast<seconds>(tEnd - tStart).count();
	return static_cast<double>(dt);
}

} // anonymous namespace


namespace Lz4Mt {

Benchmark::Benchmark()
	: enable(false)
	, nIter(3)
{}

Benchmark::~Benchmark()
{}

int Benchmark::measure(
	  Lz4MtContext& cx
	, const Lz4MtStreamDescriptor& sd
) {
	using namespace std;

	auto* ctx = &cx;
	const bool singleThread = 0 != (ctx->mode & LZ4MT_MODE_SEQUENTIAL);

	auto totalFileSize = 0.0;
	auto totalCompressSize = 0.0;
	auto totalCompressTime = 0.0;
	auto totalDecompressTime = 0.0;

	const auto* fmt = "%1i-%-14.14s :%10.0f ->%10.0f (%6.2f%%)"
					  ",%7.1f MiB/s, %7.1f MiB/s\r";

	const auto TIMELOOP = 2.0;	// sec
	for(const auto& filename : files) {
		vector<char> inpBuf;
		{
			if(!openIstream(ctx, filename)) {
				std::cerr << "Pb opening " << filename << "\n";
				return 11;
			}
			inpBuf.resize(static_cast<size_t>(getFilesize(filename)));

			std::cerr << "Loading " << filename << "...        \r";
			size_t readSize = ctx->read(ctx, inpBuf.data()
									  , static_cast<int>(inpBuf.size()));
			closeIstream(ctx);

			if(readSize != inpBuf.size()) {
				std::cerr << "\nError: problem reading file '"
						  << filename << "' !!    \n";
				return 13;
			}
		}

		const auto inpHash = XXH32(inpBuf.data()
								   , static_cast<int>(inpBuf.size()), 0);

		vector<char> outBuf;
		struct ChunkParamters {
			int		id;
			char*	inpBuffer;
			size_t	inpSize;
			char*	outBuffer;
			size_t	outSize;
			size_t	cmpSize;
			size_t	decSize;
		};
		std::vector<ChunkParamters> chunkParameterss;

		size_t chunkSize	= (1 << (8 + (2 * sd.bd.blockMaximumSize)));
		auto maxChunkSize	= ctx->compressBound(static_cast<int>(chunkSize));
		auto nChunk			= (inpBuf.size() / chunkSize) + 1;

		{
			outBuf.resize(nChunk * maxChunkSize);
			chunkParameterss.resize(nChunk);

			auto r = inpBuf.size();
			for(auto& e : chunkParameterss) {
				auto i = static_cast<int>(&e - &chunkParameterss[0]);
				e.id		= i;
				e.inpBuffer	= &inpBuf[i * chunkSize];
				e.inpSize	= r > chunkSize ? chunkSize : r;
				e.outBuffer	= &outBuf[i * maxChunkSize];
				e.outSize	= maxChunkSize;
				e.cmpSize	= 0;
				e.decSize	= 0;
				r -= e.inpSize;
			}
		}

		std::vector<future<void>> futures(chunkParameterss.size());

		fprintf(stderr, "\r%79s\r", "");

		const auto filesize = static_cast<double>(inpBuf.size());
		auto cmpSize = 0.0;
		auto minCompressionTime   = 10000000.0;
		auto minDecompressionTime = 10000000.0;

		for(int iLoop = 1, nLoop = nIter; iLoop <= nLoop; ++iLoop) {
			auto ratio = 0.0;

			// compression
			{
				auto f = [=, &futures] (ChunkParamters* cp) {
					if(singleThread && cp->id > 0) {
						futures[cp->id-1].wait();
					}
					cp->cmpSize = ctx->compress(
						  cp->inpBuffer
						, cp->outBuffer
						, static_cast<int>(cp->inpSize)
						, static_cast<int>(cp->outSize)
					);
				};

				auto* out_buff = outBuf.data();
				// warmimg up memory
				for(size_t i = 0; i < outBuf.size(); ++i) {
					out_buff[i] = static_cast<char>(i);
				}

				auto ms0 = getSyncTime();
				auto ms1 = ms0;
				int loopCount = 0;

				for(;;) {
					for(auto& e : chunkParameterss) {
						futures[e.id] = async(launch::async, f, &e);
					}
					for(auto& e : futures) {
						e.wait();
					}
					ms1 = getTime();
					if(getTimeSpan(ms0, ms1) >= TIMELOOP) {
						break;
					}
					++loopCount;
				}

				auto dt = getTimeSpan(ms0, ms1) / (double) loopCount;
				minCompressionTime = min(minCompressionTime, dt);

				cmpSize = 0.0;
				for(auto& c : chunkParameterss) {
					cmpSize += static_cast<double>(c.cmpSize);
				}
			}

			ratio = cmpSize / filesize * 100.0;
			fprintf(stderr, fmt
				, iLoop
				, filename.c_str()
				, filesize
				, cmpSize
				, ratio
				, filesize / 1024.0 / 1024.0 / minCompressionTime
				, filesize / 1024.0 / 1024.0 / minDecompressionTime
			);

			// decompression
			{
				auto f = [=, &futures] (ChunkParamters* cp) {
					if(singleThread && cp->id > 0) {
						futures[cp->id-1].wait();
					}
					cp->decSize = ctx->decompress(
						  cp->outBuffer
						, cp->inpBuffer
						, static_cast<int>(cp->cmpSize)
						, static_cast<int>(cp->inpSize)
					);
				};

				auto* in_buff = inpBuf.data();
				for(size_t i = 0;i < inpBuf.size(); ++i) {
					in_buff[i] = 0;
				}

				auto ms0 = getSyncTime();
				auto ms1 = ms0;
				int loopCount = 0;

				for(;;) {
					for(auto& e : chunkParameterss) {
						futures[e.id] = async(launch::async, f, &e);
					}
					for(auto& e : futures) {
						e.wait();
					}
					ms1 = getTime();
					if(getTimeSpan(ms0, ms1) >= TIMELOOP) {
						break;
					}
					++loopCount;
				}

				auto dt = getTimeSpan(ms0, ms1) / (double) loopCount;
				minDecompressionTime = min(minDecompressionTime, dt);
			}

			fprintf(stderr, fmt
				, iLoop
				, filename.c_str()
				, filesize
				, cmpSize
				, ratio
				, filesize / 1024.0 / 1024.0 / minCompressionTime
				, filesize / 1024.0 / 1024.0 / minDecompressionTime
			);

			const auto outHash = XXH32(
				  inpBuf.data()
				, static_cast<int>(inpBuf.size())
				, 0
			);

			if(inpHash != outHash) {
		 		fprintf(stderr
					, "\n!!! WARNING !!! "
					  "%14s : Invalid Checksum : %x != %x\n"
					, filename.c_str()
					, inpHash
					, outHash
				);
				break;
			}
		}
		fprintf(stderr, "\n");

		totalFileSize += filesize;
		totalCompressSize += cmpSize;
		totalCompressTime += minCompressionTime;
		totalDecompressTime += minDecompressionTime;
	}

	if(!files.empty()) {
		fprintf(stderr, fmt
			, 0
			, "  TOTAL"
			, totalFileSize
			, totalCompressSize
			, totalCompressSize / totalFileSize * 100.0
			, totalFileSize / 1024.0 / 1024.0 / totalCompressTime
			, totalFileSize / 1024.0 / 1024.0 / totalDecompressTime
		);
		fprintf(stderr, "\n");
	}

	return 0;
}

} // namespace Lz4Mt
