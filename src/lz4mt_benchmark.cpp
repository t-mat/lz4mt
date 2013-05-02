#include <cassert>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <iomanip>
#include <map>
#include <numeric>
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
	const auto t0 = getTime();
	for(;;) {
		const auto t = getTime();
		if(t0 != t) {
			return t;
		}
	}
}

double getTimeSpan(const TimePoint& tStart, const TimePoint& tEnd) {
	using namespace std::chrono;
	const auto dt = duration_cast<seconds>(tEnd - tStart).count();
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

	const auto msgClearLine = [] {
		cerr << "\r" << setw(79) << " " << "\r";
	};

	const auto msgNewline = [] {
		cerr << endl;
	};

	const auto msgErrOpening = [](const string& filename) {
		cerr << "Error: problem opening " << filename << endl;
	};

	const auto msgLoading = [](const string& filename) {
		cerr << "Loading " << filename << "...\r";
		cerr.flush();
	};

	const auto msgErrReading = [](const string& filename) {
		cerr << endl
			 << "Error: problem reading file " << filename << endl;
	};

	const auto msgErrChecksum = []
		(const string& filename, uint32_t inpHash, uint32_t outHash)
	{
		cerr << endl
			 << "!!! WARNING !!! "
			 << setw(14) << filename
			 << " : Invalid Checksum : "
			 << setfill('0') << hex
			 << setw(8) << inpHash
			 << " != "
			 << setw(8) << outHash
			 << endl << setfill(' ') << dec;
	};

	const auto msgReport = []
		(  const string& filename
		 , int iLoop
		 , size_t filesize
		 , size_t cmpSize
		 , double minCompressionTime
		 , double minDecompressionTime
		)
	{
		cerr << iLoop << '-'
			 << setw(14) << left << filename
			 << " :"
			 << setw(10) << right << filesize
			 << " ->"
			 << setw(10) << cmpSize;

		const auto dFilesize = static_cast<double>(filesize);
		const auto dFilesizeMib = dFilesize / 1024.0 / 1024.0;

		cerr.precision(2);
		cerr << fixed
			 << " ("
			 << setw(6) << static_cast<double>(cmpSize) * 100.0 / dFilesize
			 << "%),";

		cerr.precision(1);
		cerr << setw(7) << dFilesizeMib / minCompressionTime << " MiB/s, "
			 << setw(7) << dFilesizeMib / minDecompressionTime << " MiB/s"
			 << "\r";

		cerr.flush();
	};

	auto* ctx = &cx;
	const bool singleThread = 0 != (ctx->mode & LZ4MT_MODE_SEQUENTIAL);
	size_t totalFileSize = 0;
	size_t totalCompressSize = 0;
	double totalCompressTime = 0.0;
	double totalDecompressTime = 0.0;
	const auto TIMELOOP = 2.0;	// sec

	for(const auto& filename : files) {
		vector<char> inpBuf(static_cast<size_t>(getFilesize(filename)));
		{
			if(!openIstream(ctx, filename)) {
				msgErrOpening(filename);
				return 11;
			}

			msgLoading(filename);
			const size_t readSize = ctx->read(ctx, inpBuf.data()
									  , static_cast<int>(inpBuf.size()));
			closeIstream(ctx);

			if(inpBuf.size() != readSize) {
				msgErrReading(filename);
				return 13;
			}
		}
		msgClearLine();

		const auto inpHash =
			XXH32(inpBuf.data(), static_cast<int>(inpBuf.size()), 0);
		const size_t chunkSize	= (1 << (8 + (2 * sd.bd.blockMaximumSize)));
		const auto nChunk		= (inpBuf.size() / chunkSize) + 1;
		const auto maxChunkSize	=
			static_cast<size_t>(
				ctx->compressBound(static_cast<int>(chunkSize)));

		vector<char> outBuf(nChunk * maxChunkSize);

		struct Chunk {
			int		id;
			char*	inpPtr;
			size_t	inpSize;
			char*	outPtr;
			size_t	outSize;
			size_t	cmpSize;
			size_t	decSize;
		};

		vector<Chunk> chunks(nChunk);
		{
			auto r = inpBuf.size();
			for(auto& e : chunks) {
				const auto i = static_cast<int>(&e - chunks.data());
				e.id		= i;
				e.inpPtr	= &inpBuf[i * chunkSize];
				e.inpSize	= r > chunkSize ? chunkSize : r;
				e.outPtr	= &outBuf[i * maxChunkSize];
				e.outSize	= maxChunkSize;
				e.cmpSize	= 0;
				e.decSize	= 0;
				r -= e.inpSize;
			}
		}

		vector<future<void>> futures(chunks.size());

		const auto b = [=, &futures, &chunks]
			(function<void(Chunk*)> fChunk)
		{
			const auto t0 = getSyncTime();
			auto t1 = t0;
			int loopCount = 0;

			while(getTimeSpan(t0, t1 = getTime()) < TIMELOOP) {
				for(auto& e : chunks) {
					futures[e.id] = async(launch::async, fChunk, &e);
				}
				for(auto& e : futures) {
					e.wait();
				}
				++loopCount;
			}

			return getTimeSpan(t0, t1) / static_cast<double>(loopCount);
		};

		auto minCmpTime = 10000000.0;
		auto minDecTime = 10000000.0;
		size_t cmpSize = 0;
		for(int iLoop = 1, nLoop = nIter; iLoop <= nLoop; ++iLoop) {
			msgReport(filename, iLoop, inpBuf.size()
					  , cmpSize, minCmpTime, minDecTime);

			// compression
			iota(outBuf.begin(), outBuf.end(), 0);
			const auto cmpTime = b(
				[ctx, singleThread, &futures] (Chunk* cp) {
					if(singleThread && cp->id > 0) {
						futures[cp->id-1].wait();
					}
					cp->cmpSize = ctx->compress(
						  cp->inpPtr
						, cp->outPtr
						, static_cast<int>(cp->inpSize)
						, static_cast<int>(cp->outSize)
					);
				}
			);
			minCmpTime = min(minCmpTime, cmpTime);

			if(1 == iLoop) {
				cmpSize = 0;
				for(const auto& c : chunks) {
					cmpSize += c.cmpSize;
				}
			}

			msgReport(filename, iLoop, inpBuf.size()
					  , cmpSize, minCmpTime, minDecTime);

			// decompression
			fill(inpBuf.begin(), inpBuf.end(), 0);
			const auto decTime = b(
				[ctx, singleThread, &futures] (Chunk* cp) {
					if(singleThread && cp->id > 0) {
						futures[cp->id-1].wait();
					}
					cp->decSize = ctx->decompress(
						  cp->outPtr
						, cp->inpPtr
						, static_cast<int>(cp->cmpSize)
						, static_cast<int>(cp->inpSize)
					);
				}
			);
			minDecTime = min(minDecTime, decTime);

			msgReport(filename, iLoop, inpBuf.size()
					  , cmpSize, minCmpTime, minDecTime);

			const auto outHash =
				XXH32(inpBuf.data(), static_cast<int>(inpBuf.size()), 0);

			if(inpHash != outHash) {
				msgErrChecksum(filename, inpHash, outHash);
				break;
			}
		}
		msgNewline();

		totalFileSize += inpBuf.size();
		totalCompressSize += cmpSize;
		totalCompressTime += minCmpTime;
		totalDecompressTime += minDecTime;
	}

	if(!files.empty()) {
		msgReport("  TOTAL", 0, totalFileSize, totalCompressSize
				  , totalCompressTime, totalDecompressTime);
		msgNewline();
	}

	return 0;
}

} // namespace Lz4Mt
