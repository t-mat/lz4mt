#include <cassert>
#include <cfloat>
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
	, pause(false)
	, nIter(3)
	, files()
	, openIstream()
	, closeIstream()
	, getFilesize()
{}

Benchmark::~Benchmark()
{}

int Benchmark::measure(
	  Lz4MtContext& cx
	, const Lz4MtStreamDescriptor& sd
) {
	auto& logger = std::cerr;

	const auto msgClearLine = [&logger] {
		logger << "\r" << std::setw(79) << " " << "\r";
	};

	const auto msgNewline = [&logger] {
		logger << std::endl;
	};

	const auto msgErrOpening = [&logger](const std::string& filename) {
		logger << "Error: problem opening " << filename << std::endl;
	};

	const auto msgLoading = [&logger](const std::string& filename) {
		logger << "Loading " << filename << "...\r";
		logger.flush();
	};

	const auto msgErrReading = [&logger](const std::string& filename) {
		logger << std::endl
			 << "Error: problem reading file " << filename << std::endl;
	};

	const auto msgErrChecksum = [&logger]
		(const std::string& filename, uint32_t inpHash, uint32_t outHash)
	{
		logger << std::endl
			 << "!!! WARNING !!! "
			 << std::setw(14) << filename
			 << " : Invalid Checksum : "
			 << std::setfill('0') << std::hex
			 << std::setw(8) << inpHash
			 << " != "
			 << std::setw(8) << outHash
			 << std::endl;
	};

	const auto msgReport = [&logger]
		(  const std::string& filename
		 , int iLoop
		 , size_t filesize
		 , size_t cmpSize
		 , double minCompressionTime
		 , double minDecompressionTime
		)
	{
		logger << iLoop << '-'
			 << std::setw(14) << std::left << filename
			 << " :"
			 << std::setw(10) << std::right << filesize
			 << " ->"
			 << std::setw(10) << cmpSize;

		const auto dFilesize = static_cast<double>(filesize);
		const auto dFilesizeMib = dFilesize / 1024.0 / 1024.0;

		logger.precision(2);
		logger << std::fixed
			<< " ("
			<< std::setw(6)
			<< static_cast<double>(cmpSize) * 100.0 / dFilesize
			<< "%),";

		logger.precision(1);
		logger
			<< std::setw(7)
			<< dFilesizeMib / minCompressionTime << " MiB/s, "
			<< std::setw(7)
			<< dFilesizeMib / minDecompressionTime << " MiB/s"
			<< "\r";

		logger.flush();
	};

	auto* ctx = &cx;
	const bool singleThread = 0 != (ctx->mode & LZ4MT_MODE_SEQUENTIAL);
	size_t totalFileSize = 0;
	size_t totalCompressSize = 0;
	double totalCompressTime = 0.0;
	double totalDecompressTime = 0.0;
	const auto TIMELOOP = 2.0;	// sec

	for(const auto& filename : files) {
		std::vector<char> inpBuf(
			static_cast<size_t>(getFilesize(filename))
		);

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
		const auto chunkSize =
				(size_t(1) << (8 + (2 * sd.bd.blockMaximumSize)));
		const auto nChunk		= (inpBuf.size() / chunkSize) + 1;
		const auto maxChunkSize	=
			static_cast<size_t>(
				ctx->compressBound(static_cast<int>(chunkSize)));

		std::vector<char> outBuf(nChunk * maxChunkSize);

		struct Chunk {
			int		id;
			char*	inpPtr;
			size_t	inpSize;
			char*	outPtr;
			size_t	outSize;
			size_t	cmpSize;
			size_t	decSize;
		};

		std::vector<Chunk> chunks(nChunk);
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

		std::vector<std::future<void>> futures(chunks.size());

		const auto b = [=, &futures, &chunks]
			(std::function<void(Chunk*)> fChunk) -> double
		{
			const auto t0 = getSyncTime();
			auto t1 = t0;
			int loopCount = 0;

			while(getTimeSpan(t0, t1 = getTime()) < TIMELOOP) {
				for(auto& e : chunks) {
					futures[e.id] = async(std::launch::async, fChunk, &e);
				}
				for(auto& e : futures) {
					e.wait();
				}
				++loopCount;
			}

			return getTimeSpan(t0, t1) / static_cast<double>(loopCount);
		};

		auto minCmpTime = DBL_MAX;
		auto minDecTime = DBL_MAX;
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
						, ctx->compressionLevel
					);
				}
			);
			minCmpTime = std::min(minCmpTime, cmpTime);

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
			minDecTime = std::min(minDecTime, decTime);

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
