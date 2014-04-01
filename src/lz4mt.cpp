#include <array>
#include <atomic>
#include <cassert>
#include <future>
#include <mutex>
#include <vector>
#include "lz4mt.h"
#include "lz4mt_xxh32.h"
#include "lz4mt_mempool.h"
#include "lz4mt_compat.h"

#include "lz4.h"
#include "lz4hc.h"
#include <string.h>


namespace {

const uint32_t LZ4S_MAGICNUMBER = 0x184D2204;
const uint32_t LZ4S_MAGICNUMBER_SKIPPABLE_MIN = 0x184D2A50;
const uint32_t LZ4S_MAGICNUMBER_SKIPPABLE_MAX = 0x184D2A5F;
const uint32_t LZ4S_BLOCKSIZEID_DEFAULT = 7;
const uint32_t LZ4S_CHECKSUM_SEED = 0;
const uint32_t LZ4S_EOS = 0;
const uint32_t LZ4S_MAX_HEADER_SIZE = 4 + 2 + 8 + 4 + 1;
const uint32_t LZ4S_MIN_STREAM_BUFSIZE = (1024 + 64) * 1024;
const uint32_t LZ4S_CACHELINE = 64;

const uint32_t LZ4MT_SRC_BITS_INCOMPRESSIBLE_MASK = 1U << 31;
const uint32_t LZ4MT_SRC_BITS_SIZE_MASK = ~LZ4MT_SRC_BITS_INCOMPRESSIBLE_MASK;

typedef std::unique_ptr<Lz4Mt::MemPool::Buffer> BufferPtr;

int getBlockSize(int bdBlockMaximumSize) {
	assert(bdBlockMaximumSize >= 4 && bdBlockMaximumSize <= 7);
	return (1 << (8 + (2 * bdBlockMaximumSize)));
}

uint32_t getCheckBits_FromXXH(uint32_t xxh) {
	return (xxh >> 8) & 0xff;
}

bool isMagicNumber(uint32_t magic) {
	return LZ4S_MAGICNUMBER == magic;
}

bool isSkippableMagicNumber(uint32_t magic) {
	return magic >= LZ4S_MAGICNUMBER_SKIPPABLE_MIN
		&& magic <= LZ4S_MAGICNUMBER_SKIPPABLE_MAX;
}

bool isEos(uint32_t srcBits) {
	return LZ4S_EOS == srcBits;
}

bool isIncompless(uint32_t srcBits) {
	return 0 != (srcBits & LZ4MT_SRC_BITS_INCOMPRESSIBLE_MASK);
}

template<typename T>
uint32_t makeIncompless(T size) {
	return size | static_cast<T>(LZ4MT_SRC_BITS_INCOMPRESSIBLE_MASK);
}

int getSrcSize(uint32_t srcBits) {
	return static_cast<int>(srcBits & LZ4MT_SRC_BITS_SIZE_MASK);
}

char flgToChar(const Lz4MtFlg& flg) {
	return static_cast<char>(
		  ((flg.presetDictionary  & 1) << 0)
		| ((flg.reserved1         & 1) << 1)
		| ((flg.streamChecksum    & 1) << 2)
		| ((flg.streamSize        & 1) << 3)
		| ((flg.blockChecksum     & 1) << 4)
		| ((flg.blockIndependence & 1) << 5)
		| ((flg.versionNumber     & 3) << 6)
	);
}

Lz4MtFlg charToFlg(char c) {
	Lz4MtFlg flg = { 0 };
	flg.presetDictionary	= (c >> 0) & 1;
	flg.reserved1			= (c >> 1) & 1;
	flg.streamChecksum		= (c >> 2) & 1;
	flg.streamSize			= (c >> 3) & 1;
	flg.blockChecksum		= (c >> 4) & 1;
	flg.blockIndependence	= (c >> 5) & 1;
	flg.versionNumber		= (c >> 6) & 3;
	return flg;
}

char bdToChar(const Lz4MtBd& bd) {
	return static_cast<char>(
		  ((bd.reserved3        & 15) << 0)
		| ((bd.blockMaximumSize &  7) << 4)
		| ((bd.reserved2        &  1) << 7)
	);
}

Lz4MtBd charToBd(char c) {
	Lz4MtBd bd = { 0 };
	bd.reserved3		= (c >> 0) & 15;
	bd.blockMaximumSize	= (c >> 4) &  7;
	bd.reserved2		= (c >> 7) &  1;
	return bd;
}

size_t storeU32(void* p, uint32_t v) {
	auto* q = reinterpret_cast<char*>(p);
	q[0] = static_cast<char>(v >> (8*0));
	q[1] = static_cast<char>(v >> (8*1));
	q[2] = static_cast<char>(v >> (8*2));
	q[3] = static_cast<char>(v >> (8*3));
	return sizeof(v);
}

size_t storeU64(void* p, uint64_t v) {
	auto* q = reinterpret_cast<char*>(p);
	storeU32(q+0, static_cast<uint32_t>(v >> (8*0)));
	storeU32(q+4, static_cast<uint32_t>(v >> (8*4)));
	return sizeof(v);
}

uint32_t loadU32(const void* p) {
	auto* q = reinterpret_cast<const uint8_t*>(p);
	return (static_cast<uint32_t>(q[0]) << (8*0))
		 | (static_cast<uint32_t>(q[1]) << (8*1))
		 | (static_cast<uint32_t>(q[2]) << (8*2))
		 | (static_cast<uint32_t>(q[3]) << (8*3));
}

uint64_t loadU64(const void* p) {
	auto* q = reinterpret_cast<const uint8_t*>(p);
	return (static_cast<uint64_t>(loadU32(q+0)) << (8*0))
		 | (static_cast<uint64_t>(loadU32(q+4)) << (8*4));
}

Lz4MtResult
validateStreamDescriptor(const Lz4MtStreamDescriptor* sd) {
	if(1 != sd->flg.versionNumber) {
		return LZ4MT_RESULT_INVALID_VERSION;
	}
	if(0 != sd->flg.presetDictionary) {
		///	@TODO: Implement Preset Dictionary.
		return LZ4MT_RESULT_PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET;
	}
	if(0 != sd->flg.reserved1) {
		return LZ4MT_RESULT_INVALID_HEADER_RESERVED1;
	}
	if(sd->bd.blockMaximumSize < 4 || sd->bd.blockMaximumSize > 7) {
		return LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE;
	}
	if(0 != sd->bd.reserved3) {
		return LZ4MT_RESULT_INVALID_HEADER_RESERVED3;
	}
	if(0 != sd->bd.reserved2) {
		return LZ4MT_RESULT_INVALID_HEADER_RESERVED2;
	}
	return LZ4MT_RESULT_OK;
}

class Ctx {
public:
	Ctx(Lz4MtContext* lz4MtContext)
		: lz4MtContext(lz4MtContext)
		, mutResult()
		, atmQuit(false)
	{}

	bool error() const {
		Lock lock(mutResult);
		return LZ4MT_RESULT_OK != lz4MtContext->result;
	}

	Lz4MtResult setResult(Lz4MtResult result) {
		Lock lock(mutResult);
		auto& r = lz4MtContext->result;
		if(LZ4MT_RESULT_OK == r || LZ4MT_RESULT_ERROR == r) {
			r = result;
		}
		return r;
	}

	Lz4MtResult result() {
		Lock lock(mutResult);
		return lz4MtContext->result;
	}

	int compressionLevel() const {
		return lz4MtContext->compressionLevel;
	}

	uint32_t readU32() {
		if(error()) {
			return 0;
		}

		char d[sizeof(uint32_t)];
		if(sizeof(d) != lz4MtContext->read(lz4MtContext, d, sizeof(d))) {
			setResult(LZ4MT_RESULT_ERROR);
			return 0;
		}
		return loadU32(d);
	}

	bool writeU32(uint32_t v) {
		char d[sizeof(v)];
		storeU32(d, v);
		return writeBin(d, sizeof(d));
	}

	bool writeBin(const void* ptr, int size) {
		if(error()) {
			return false;
		}
		if(size != lz4MtContext->write(lz4MtContext, ptr, size)) {
			setResult(LZ4MT_RESULT_ERROR);
			return false;
		}
		return true;
	}

	Lz4MtMode mode() const {
		return lz4MtContext->mode;
	}

	int read(void* dst, int dstSize) {
		return lz4MtContext->read(lz4MtContext, dst, dstSize);
	}

	int readSeek(int offset) {
		return lz4MtContext->readSeek(lz4MtContext, offset);
	}

	int readEof() {
		return lz4MtContext->readEof(lz4MtContext);
	}

	int readSkippable(uint32_t magicNumber, size_t size) {
		return lz4MtContext->readSkippable(lz4MtContext, magicNumber, size);
	}

	int write(const void* src, int srcSize) {
		return lz4MtContext->write(lz4MtContext, src, srcSize);
	}

	int compress(const char* src, char* dst, int isize, int maxOutputSize) {
		return lz4MtContext->compress(src, dst, isize, maxOutputSize, lz4MtContext->compressionLevel);
	}

	int decompress(const char* src, char* dst, int isize, int maxOutputSize) {
		return lz4MtContext->decompress(src, dst, isize, maxOutputSize);
	}

	Lz4MtResult quit(Lz4MtResult result) {
		setResult(result);
		atmQuit = true;
		return result;
	}

	bool isQuit() const {
		return atmQuit;
	}

private:
	typedef std::unique_lock<std::mutex> Lock;
	Lz4MtContext* lz4MtContext;
	mutable std::mutex mutResult;
	std::atomic<bool> atmQuit;
};


struct Params {
	Params(const Lz4MtContext* lz4MtContext, const Lz4MtStreamDescriptor* sd)
		: nBlockMaximumSize	 (getBlockSize(sd->bd.blockMaximumSize))
		, blockCheckSumBytes (sd->flg.blockChecksum ? 4 : 0)
		, streamChecksum	 (0 != sd->flg.streamChecksum)
		, blockIndependence	 (0 != sd->flg.blockIndependence)
		, singleThread		 (0 != (lz4MtContext->mode & LZ4MT_MODE_SEQUENTIAL))
		, nPool				 (singleThread ? 1 : Lz4Mt::getHardwareConcurrency() + 1)
		, launch			 (singleThread ? Lz4Mt::launch::deferred : std::launch::async)
	{}

	int nBlockMaximumSize;
	int blockCheckSumBytes;
	bool streamChecksum;
	bool blockIndependence;
	bool singleThread;
	unsigned nPool;
	Lz4Mt::launch::Type launch;
};


class BlockDependentCompressor {
public:
	BlockDependentCompressor(int compressionLevel, const char* inputBuffer)
		: isHc(compressionLevel >= 3)
		, lz4Ctx(new char[
			isHc ? LZ4_sizeofStreamStateHC()
			     : LZ4_sizeofStreamState()
		  ])
		, compressFunction(
			isHc ? LZ4_compressHC_limitedOutput_continue
			     : LZ4_compress_limitedOutput_continue
		  )
		, translateFunction(
			isHc ? LZ4_slideInputBufferHC
			     : LZ4_slideInputBuffer
		)
	{
		if(isHc) {
			LZ4_resetStreamStateHC(lz4Ctx.get(), inputBuffer);
		} else {
			LZ4_resetStreamState(lz4Ctx.get(), inputBuffer);
		}
	}

	int compress(const char* source, char* dest, int inputSize, int maxOutputSize) {
		return compressFunction(lz4Ctx.get(), source, dest, inputSize, maxOutputSize);
	}

	char* translate() {
		return translateFunction(lz4Ctx.get());
	}

private:
	const bool isHc;
	const std::unique_ptr<char[]> lz4Ctx;
	const std::function<int(void*, const char*, char*, int, int)> compressFunction;
	const std::function<char*(void*)> translateFunction;
};


Lz4MtResult
makeHeader(Ctx& ctx, const Lz4MtStreamDescriptor* sd)
{
	char d[LZ4S_MAX_HEADER_SIZE] = { 0 };
	auto p = &d[0];

	const auto r = validateStreamDescriptor(sd);
	if(LZ4MT_RESULT_OK != r) {
		return ctx.quit(r);
	}
	p += storeU32(p, LZ4S_MAGICNUMBER);

	const auto* sumBegin = p;
	*p++ = flgToChar(sd->flg);
	*p++ = bdToChar(sd->bd);
	if(sd->flg.streamSize) {
		assert(sd->streamSize);
		p += storeU64(p, sd->streamSize);
	}
	if(sd->flg.presetDictionary) {
		p += storeU32(p, sd->dictId);
	}

	const auto sumSize = static_cast<int>(p - sumBegin);
	const auto h = Lz4Mt::Xxh32(sumBegin, sumSize, LZ4S_CHECKSUM_SEED).digest();
	*p++ = static_cast<char>(getCheckBits_FromXXH(h));
	assert(p <= std::end(d));

	const auto writeSize = static_cast<int>(p - d);
	if(writeSize != ctx.write(d, writeSize)) {
		return ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_HEADER);
	}

	return LZ4MT_RESULT_OK;
}


Lz4MtResult
compress(Ctx& ctx, const Params& params, Lz4Mt::Xxh32& xxhStream)
{
	Lz4Mt::MemPool srcBufferPool(params.nBlockMaximumSize, params.nPool);
	Lz4Mt::MemPool dstBufferPool(params.nBlockMaximumSize, params.nPool);
	std::vector<std::future<void>> futures;

	const auto f =
		[&futures, &dstBufferPool, &xxhStream, &params, &ctx]
		(int i, Lz4Mt::MemPool::Buffer* srcRawPtr, int srcSize)
	{
		BufferPtr src(srcRawPtr);
		if(ctx.error()) {
			return;
		}

		const auto* srcPtr = src->data();
		BufferPtr dst(dstBufferPool.alloc());
		auto* cmpPtr = dst->data();
		const auto cmpSize = ctx.compress(srcPtr, cmpPtr, srcSize, srcSize);
		const bool incompressible = (cmpSize <= 0);
		const auto* cPtr  = incompressible ? srcPtr  : cmpPtr;
		const auto  cSize = incompressible ? srcSize : cmpSize;

		std::future<uint32_t> futureBlockHash;
		if(params.blockCheckSumBytes) {
			futureBlockHash = std::async(params.launch, [=] {
				return Lz4Mt::Xxh32(cPtr, cSize, LZ4S_CHECKSUM_SEED).digest();
			});
		}

		if(incompressible) {
			dst.reset();
		}

		if(i > 0) {
			futures[i-1].wait();
		}

		std::future<void> futureStreamHash;
		if(params.streamChecksum) {
			futureStreamHash = std::async(params.launch, [=, &xxhStream] {
				xxhStream.update(srcPtr, srcSize);
			});
		}

		if(incompressible) {
			ctx.writeU32(makeIncompless(cSize));
			ctx.writeBin(srcPtr, srcSize);
		} else {
			ctx.writeU32(cSize);
			ctx.writeBin(cmpPtr, cmpSize);
		}

		if(futureBlockHash.valid()) {
			ctx.writeU32(futureBlockHash.get());
		}

		if(futureStreamHash.valid()) {
			futureStreamHash.wait();
		}
	};

	for(int i = 0;; ++i) {
		BufferPtr src(srcBufferPool.alloc());
		auto* srcPtr = src->data();
		const auto srcSize = src->size();
		const auto readSize = ctx.read(srcPtr, static_cast<int>(srcSize));

		if(0 == readSize) {
			break;
		}

		if(params.singleThread) {
			f(0, src.release(), readSize);
		} else {
			futures.emplace_back(std::async(params.launch, f, i, src.release(), readSize));
		}
	}

	for(auto& e : futures) {
		e.wait();
	}

	return LZ4MT_RESULT_OK;
}


Lz4MtResult
compressBlockDependency(Ctx& ctx, const Params& params, Lz4Mt::Xxh32& xxhStream)
{
	const auto inputBufferSize = [&]() -> size_t {
		// NOTE for "-> size_t" :
		//		It's a workaround for g++-4.6's strange warning.
		const auto s = params.nBlockMaximumSize + 65536;
		return std::max(s, static_cast<decltype(s)>(LZ4S_MIN_STREAM_BUFSIZE));
	}();

	const size_t nPool = 1;
	Lz4Mt::MemPool srcBufferPool(inputBufferSize, nPool);
	Lz4Mt::MemPool dstBufferPool(params.nBlockMaximumSize + LZ4S_CACHELINE, nPool);

	const BufferPtr src(srcBufferPool.alloc());
	const BufferPtr dst(dstBufferPool.alloc());

	auto* const srcBuf = src->data();
	auto* const srcEnd = srcBuf + src->size();
	auto* const dstBuf = dst->data();

	auto* in_start = srcBuf;

	BlockDependentCompressor bdc(ctx.compressionLevel(), srcBuf);

	for(;;) {
		if((in_start + params.nBlockMaximumSize) > srcEnd) {
			in_start = bdc.translate();
		}

		const auto inSize = ctx.read(in_start, params.nBlockMaximumSize);
		if(0 == inSize) {
			break;
		}

		if(params.streamChecksum) {
			xxhStream.update(in_start, inSize);
		}

		const auto outSize = bdc.compress(
			  in_start
			, dstBuf
			, inSize
			, inSize-1
		);

		struct WriteStat {
			int bytes;
			int header;
			char* ptr;
		};
		const auto writeStat = [&]() -> WriteStat {
			// NOTE for "-> WriteStat" :
			//		It's a workaround for g++-4.6's strange warning.
			WriteStat ws = { 0 };
			if(outSize > 0) {
				ws.bytes	= outSize;
				ws.header	= outSize;
				ws.ptr		= dstBuf;
			} else {
				ws.bytes	= inSize;
				ws.header	= makeIncompless(inSize);
				ws.ptr		= in_start;
			}
			return ws;
		} ();

		ctx.writeU32(writeStat.header);
		ctx.writeBin(writeStat.ptr, writeStat.bytes);
		if(params.blockCheckSumBytes) {
			const auto xh = Lz4Mt::Xxh32(writeStat.ptr, writeStat.bytes, LZ4S_CHECKSUM_SEED).digest();
			ctx.writeU32(xh);
		}

		in_start += inSize;
	}

	return LZ4MT_RESULT_OK;
}


Lz4MtResult
readHeader(Ctx& ctx, Lz4MtStreamDescriptor* sd)
{
	char d[LZ4S_MAX_HEADER_SIZE] = { 0 };
	auto* p = d;
	const auto* sumBegin = p;

	if(2 != ctx.read(p, 2)) {
		return ctx.quit(LZ4MT_RESULT_INVALID_HEADER);
	}
	sd->flg = charToFlg(*p++);
	sd->bd  = charToBd(*p++);

	const auto r = validateStreamDescriptor(sd);
	if(LZ4MT_RESULT_OK != r) {
		return ctx.quit(r);
	}

	const int nExInfo =
		  (sd->flg.streamSize       ? sizeof(uint64_t) : 0)
		+ (sd->flg.presetDictionary ? sizeof(uint32_t) : 0)
		+ 1
	;
	if(nExInfo != ctx.read(p, nExInfo)) {
		return ctx.quit(LZ4MT_RESULT_INVALID_HEADER);
	}

	if(sd->flg.streamSize) {
		sd->streamSize = loadU64(p);
		p += sizeof(uint64_t);
	}

	if(sd->flg.presetDictionary) {
		sd->dictId = loadU32(p);
		p += sizeof(uint32_t);
	}

	const auto sumSize   = static_cast<int>(p - sumBegin);
	const auto calHash32 = Lz4Mt::Xxh32(sumBegin, sumSize, LZ4S_CHECKSUM_SEED).digest();
	const auto calHash   = static_cast<char>(getCheckBits_FromXXH(calHash32));
	const auto srcHash   = *p++;

	assert(p <= std::end(d));

	if(srcHash != calHash) {
		return ctx.quit(LZ4MT_RESULT_INVALID_HEADER_CHECKSUM);
	}

	return LZ4MT_RESULT_OK;
}


bool
decompress(Ctx& ctx, const Params& params, Lz4Mt::Xxh32& xxhStream)
{
	Lz4Mt::MemPool srcBufferPool(params.nBlockMaximumSize, params.nPool);
	Lz4Mt::MemPool dstBufferPool(params.nBlockMaximumSize, params.nPool);
	std::vector<std::future<void>> futures;

	const auto f =
		[&futures, &dstBufferPool, &xxhStream, &params, &ctx]
		(int i, Lz4Mt::MemPool::Buffer* srcRaw, bool incompressible, uint32_t blockChecksum)
	{
		BufferPtr src(srcRaw);
		if(ctx.error() || ctx.isQuit()) {
			return;
		}

		const auto* srcPtr = src->data();
		const auto srcSize = static_cast<int>(src->size());

		std::future<uint32_t> futureBlockHash;
		if(params.blockCheckSumBytes) {
			futureBlockHash = std::async(params.launch, [=] {
				return Lz4Mt::Xxh32(srcPtr, srcSize, LZ4S_CHECKSUM_SEED).digest();
			});
		}

		if(incompressible) {
			if(i > 0) {
				futures[i-1].wait();
			}

			std::future<void> futureStreamHash;
			if(params.streamChecksum) {
				futureStreamHash = std::async(
					  params.launch
					, [&xxhStream, srcPtr, srcSize] {
						xxhStream.update(srcPtr, srcSize);
					}
				);
			}
			if(! ctx.writeBin(srcPtr, srcSize)) {
				ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK);
				return;
			}
			if(futureStreamHash.valid()) {
				futureStreamHash.wait();
			}
		} else {
			BufferPtr dst(dstBufferPool.alloc());

			auto* dstPtr = dst->data();
			const auto dstSize = dst->size();
			const auto decSize = ctx.decompress(
				srcPtr, dstPtr, srcSize, static_cast<int>(dstSize));
			if(decSize < 0) {
				ctx.quit(LZ4MT_RESULT_DECOMPRESS_FAIL);
				return;
			}

			if(i > 0) {
				futures[i-1].wait();
			}

			std::future<void> futureStreamHash;
			if(params.streamChecksum) {
				futureStreamHash = std::async(
					  params.launch
					, [&xxhStream, dstPtr, decSize] {
						xxhStream.update(dstPtr, decSize);
					}
				);
			}
			if(! ctx.writeBin(dstPtr, decSize)) {
				ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_DECODED_BLOCK);
				return;
			}

			if(futureStreamHash.valid()) {
				futureStreamHash.wait();
			}
		}

		if(futureBlockHash.valid()) {
			auto bh = futureBlockHash.get();
			if(bh != blockChecksum) {
				ctx.quit(LZ4MT_RESULT_BLOCK_CHECKSUM_MISMATCH);
				return;
			}
		}
		return;
	};

	bool eos = false;
	for(int i = 0; !eos && !ctx.isQuit() && !ctx.readEof(); ++i) {
		const auto srcBits = ctx.readU32();
		if(ctx.error()) {
			ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE);
			continue;
		}

		if(isEos(srcBits)) {
			eos = true;
			continue;
		}

		const auto srcSize = getSrcSize(srcBits);
		if(srcSize > params.nBlockMaximumSize) {
			ctx.quit(LZ4MT_RESULT_INVALID_BLOCK_SIZE);
			continue;
		}

		BufferPtr src(srcBufferPool.alloc());
		const auto readSize = ctx.read(src->data(), srcSize);
		if(srcSize != readSize || ctx.error()) {
			ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA);
			continue;
		}
		src->resize(readSize);

		const auto blockCheckSum = params.blockCheckSumBytes ? ctx.readU32() : 0;
		if(ctx.error()) {
			ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM);
			continue;
		}

		const bool incompress = isIncompless(srcBits);
		if(params.singleThread) {
			f(0, src.release(), incompress, blockCheckSum);
		} else {
			futures.emplace_back(std::async(
				  params.launch
				, f, i, src.release(), incompress, blockCheckSum
			));
		}
	}

	for(auto& e : futures) {
		e.wait();
	}

	return eos;
}


bool
decompressBlockDependency(Ctx& ctx, const Params& params, Lz4Mt::Xxh32& xxhStream)
{
	const size_t prefix64k = 64 * 1024;

	const size_t nPool = 1;
	Lz4Mt::MemPool srcBufferPool(params.nBlockMaximumSize, nPool);
	Lz4Mt::MemPool dstBufferPool(prefix64k + params.nBlockMaximumSize, nPool);

	const BufferPtr src(srcBufferPool.alloc());
	const BufferPtr dst(dstBufferPool.alloc());

	auto* dstPtr = dst->data() + prefix64k;

	bool eos = false;
	for(; !eos && !ctx.isQuit() && !ctx.readEof();) {
		const auto srcBits = ctx.readU32();
		if(ctx.error()) {
			ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE);
			continue;
		}

		if(isEos(srcBits)) {
			eos = true;
			continue;
		}

		{
			const auto srcSize = getSrcSize(srcBits);

			if(srcSize > params.nBlockMaximumSize) {
				ctx.quit(LZ4MT_RESULT_INVALID_BLOCK_SIZE);
				continue;
			}

			const auto readSize = ctx.read(src->data(), srcSize);
			if(srcSize != readSize || ctx.error()) {
				ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA);
				continue;
			}
			src->resize(srcSize);
		}

		const auto blockCheckSum = params.blockCheckSumBytes ? ctx.readU32() : 0;
		if(ctx.error()) {
			ctx.quit(LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM);
			continue;
		}

		if(params.blockCheckSumBytes) {
			const auto hash = Lz4Mt::Xxh32(src->data(), static_cast<int>(src->size()), LZ4S_CHECKSUM_SEED).digest();
			if(hash != blockCheckSum) {
				ctx.quit(LZ4MT_RESULT_BLOCK_CHECKSUM_MISMATCH);
				continue;
			}
		}

		int decodedBytes = 0;

		const bool incompress = isIncompless(srcBits);
		if(incompress) {
			if(! ctx.writeBin(src->data(), static_cast<int>(src->size()))) {
				ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK);
				continue;
			}

			if(params.streamChecksum) {
				xxhStream.update(src->data(), static_cast<int>(src->size()));
			}

			if(src->size() >= prefix64k) {
				memcpy(dst->data(), src->data() + src->size() - prefix64k, prefix64k);
				dstPtr = dst->data() + prefix64k;
				continue;
			} else {
				memcpy(dstPtr, src->data(), src->size());
				decodedBytes = static_cast<int>(src->size());
			}
		} else {
			decodedBytes = LZ4_decompress_safe_withPrefix64k(
				src->data()
				, dstPtr
				, static_cast<int>(src->size())
				, params.nBlockMaximumSize
			);
			if(decodedBytes < 0) {
				ctx.quit(LZ4MT_RESULT_DECOMPRESS_FAIL);
				continue;
			}
			
			if(params.streamChecksum) {
				xxhStream.update(dstPtr, decodedBytes);
			}

			if(! ctx.writeBin(dstPtr, decodedBytes)) {
				ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK);
				continue;
			}
		}

		dstPtr += decodedBytes;
		if(dst->data() + dst->size() - dstPtr < params.nBlockMaximumSize) {
			memcpy(dst->data(), dstPtr - prefix64k, prefix64k);
			dstPtr = dst->data() + prefix64k;
		}
	}

	return eos;
}


} // anonymous namespace


extern "C" Lz4MtContext
lz4mtInitContext()
{
	Lz4MtContext e = { LZ4MT_RESULT_OK, 0 };

	e.result			= LZ4MT_RESULT_OK;
	e.readCtx			= nullptr;
	e.read				= nullptr;
	e.readEof			= nullptr;
	e.readSkippable		= nullptr;
	e.readSeek			= nullptr;
	e.writeCtx			= nullptr;
	e.write				= nullptr;
	e.compress			= nullptr;
	e.compressBound		= nullptr;
	e.decompress		= nullptr;
	e.mode				= LZ4MT_MODE_PARALLEL;
	e.compressionLevel	= 0;

	return e;
}


extern "C" Lz4MtStreamDescriptor
lz4mtInitStreamDescriptor()
{
	Lz4MtStreamDescriptor e = { { 0 } };

	e.flg.presetDictionary	= 0;
	e.flg.streamChecksum	= 1;
	e.flg.reserved1			= 0;
	e.flg.streamSize		= 0;
	e.flg.blockChecksum		= 0;
	e.flg.blockIndependence	= 1;
	e.flg.versionNumber		= 1;

	e.bd.reserved3			= 0;
	e.bd.blockMaximumSize	= LZ4S_BLOCKSIZEID_DEFAULT;
	e.bd.reserved2			= 0;

	e.streamSize			= 0;
	e.dictId				= 0;

	return e;
}


extern "C" Lz4MtResult
lz4mtCompress(Lz4MtContext* lz4MtContext, const Lz4MtStreamDescriptor* sd)
{
	assert(lz4MtContext);
	assert(sd);

	const Params params(lz4MtContext, sd);
	Ctx ctx(lz4MtContext);

	makeHeader(ctx, sd);
	if(LZ4MT_RESULT_OK != ctx.result()) {
		return ctx.result();
	}

	Lz4Mt::Xxh32 xxhStream(LZ4S_CHECKSUM_SEED);

	if(sd->flg.blockIndependence) {
		compress(ctx, params, xxhStream);
	} else {
		compressBlockDependency(ctx, params, xxhStream);
	}
	if(LZ4MT_RESULT_OK != ctx.result()) {
		return ctx.result();
	}

	if(!ctx.writeU32(LZ4S_EOS)) {
		return ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_EOS);
	}

	if(params.streamChecksum) {
		const auto digest = xxhStream.digest();
		if(!ctx.writeU32(digest)) {
			return ctx.quit(LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM);
		}
	}

	return LZ4MT_RESULT_OK;
}


extern "C" Lz4MtResult
lz4mtDecompress(Lz4MtContext* lz4MtContext, Lz4MtStreamDescriptor* sd)
{
	assert(lz4MtContext);
	assert(sd);

	Ctx ctx(lz4MtContext);

	bool magicNumberRecognized = false;

	ctx.setResult(LZ4MT_RESULT_OK);
	while(!ctx.isQuit() && !ctx.error() && !ctx.readEof()) {
		const auto magic = ctx.readU32();
		if(ctx.error()) {
			if(ctx.readEof()) {
				ctx.setResult(LZ4MT_RESULT_OK);
			} else {
				ctx.setResult(LZ4MT_RESULT_INVALID_HEADER);
			}
			continue;
		}

		if(! isMagicNumber(magic)) {
			if(isSkippableMagicNumber(magic)) {
				const auto size = ctx.readU32();
				if(ctx.error()) {
					ctx.setResult(LZ4MT_RESULT_INVALID_HEADER_SKIPPABLE_SIZE_UNREADABLE);
				} else {
					const auto s = ctx.readSkippable(magic, size);
					if(s < 0 || ctx.error()) {
						ctx.setResult(LZ4MT_RESULT_INVALID_HEADER_CANNOT_SKIP_SKIPPABLE_AREA);
					}
				}
			} else {
				ctx.readSeek(-4);
				if(magicNumberRecognized) {
					ctx.setResult(LZ4MT_RESULT_OK);
				} else {
					ctx.setResult(LZ4MT_RESULT_INVALID_MAGIC_NUMBER);
				}
			}
			continue;
		}
		magicNumberRecognized = true;

		const auto readHeaderResult = readHeader(ctx, sd);
		if(LZ4MT_RESULT_OK != readHeaderResult) {
			continue;
		}

		const Params params(lz4MtContext, sd);
		Lz4Mt::Xxh32 xxhStream(LZ4S_CHECKSUM_SEED);

		if(params.blockIndependence) {
			decompress(ctx, params, xxhStream);
		} else {
			decompressBlockDependency(ctx, params, xxhStream);
		}

		if(!ctx.error() && params.streamChecksum) {
			const auto srcStreamChecksum = ctx.readU32();
			if(ctx.error()) {
				ctx.setResult(LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM);
				continue;
			}
			if(xxhStream.digest() != srcStreamChecksum) {
				ctx.setResult(LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH);
				continue;
			}
		}
	}

	return ctx.result();
}
