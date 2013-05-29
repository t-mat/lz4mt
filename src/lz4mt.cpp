#include <array>
#include <atomic>
#include <cassert>
#include <future>
#include <mutex>
#include <vector>
#include "lz4mt.h"
#include "lz4mt_xxh32.h"
#include "lz4mt_mempool.h"


namespace {

const uint32_t LZ4S_MAGICNUMBER = 0x184D2204;
const uint32_t LZ4S_MAGICNUMBER_SKIPPABLE_MIN = 0x184D2A50;
const uint32_t LZ4S_MAGICNUMBER_SKIPPABLE_MAX = 0x184D2A5F;
const uint32_t LZ4S_BLOCKSIZEID_DEFAULT = 7;
const uint32_t LZ4S_CHECKSUM_SEED = 0;
const uint32_t LZ4S_EOS = 0;
const uint32_t LZ4S_MAX_HEADER_SIZE = 4 + 2 + 8 + 4 + 1;

int getBlockSize(int bdBlockMaximumSize) {
	assert(bdBlockMaximumSize >= 4 && bdBlockMaximumSize <= 7);
	return (1 << (8 + (2 * bdBlockMaximumSize)));
}

uint32_t getCheckBits_FromXXH(uint32_t xxh) {
	return (xxh >> 8) & 0xff;
}

bool isSkippableMagicNumber(uint32_t magic) {
	return magic >= LZ4S_MAGICNUMBER_SKIPPABLE_MIN
		&& magic <= LZ4S_MAGICNUMBER_SKIPPABLE_MAX;
}

char flgToChar(const Lz4MtFlg& flg) {
	return static_cast<char>(
		  ((flg.presetDictionary  & 1) << 0)
		| ((flg.reserved1         & 1) << 1)
		| ((flg.streamChecksum    & 1) << 2)
		| ((flg.streamSize        & 1) << 3)
		| ((flg.blockChecksum     & 1) << 4)
		| ((flg.blockIndependance & 1) << 5)
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
	flg.blockIndependance	= (c >> 5) & 1;
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

Lz4MtBd charToBc(char c) {
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

bool error(const Lz4MtContext* ctx) {
	///@TODO: reimplement by std::condition_variable
	return LZ4MT_RESULT_OK != ctx->result;
}

Lz4MtResult setResult(Lz4MtContext* ctx, Lz4MtResult result) {
	///@TODO: reimplement by std::condition_variable
	auto& r = ctx->result;
	if(LZ4MT_RESULT_OK == r || LZ4MT_RESULT_ERROR == r) {
		r = result;
	}
	return r;
}

uint32_t readU32(Lz4MtContext* ctx) {
	if(error(ctx)) {
		return 0;
	}

	char d[sizeof(uint32_t)];
	if(sizeof(d) != ctx->read(ctx, d, sizeof(d))) {
		ctx->result = LZ4MT_RESULT_ERROR;
		return 0;
	}
	return loadU32(d);
}

bool writeU32(Lz4MtContext* ctx, uint32_t v) {
	if(error(ctx)) {
		return false;
	}

	char d[sizeof(v)];
	storeU32(d, v);
	if(sizeof(d) != ctx->write(ctx, d, sizeof(d))) {
		ctx->result = LZ4MT_RESULT_ERROR;
		return false;
	}
	return true;
}

bool writeBin(Lz4MtContext* ctx, const void* ptr, int size) {
	if(error(ctx)) {
		return false;
	}
	if(size != ctx->write(ctx, ptr, size)) {
		ctx->result = LZ4MT_RESULT_ERROR;
		return false;
	}
	return true;
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
		return LZ4MT_RESULT_INVALID_HEADER;
	}
	if(0 == sd->flg.blockIndependance) {
		///	@TODO: Implement Block Dependency. lz4: r96 - https://code.google.com/p/lz4/source/detail?r=96
		return LZ4MT_RESULT_BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET;
	}
	if(sd->bd.blockMaximumSize < 4 || sd->bd.blockMaximumSize > 7) {
		return LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE;
	}
	if(0 != sd->bd.reserved3) {
		return LZ4MT_RESULT_INVALID_HEADER;
	}
	if(0 != sd->bd.reserved2) {
		return LZ4MT_RESULT_INVALID_HEADER;
	}
	return LZ4MT_RESULT_OK;
}


} // anonymous namespace


extern "C" Lz4MtContext
lz4mtInitContext()
{
	Lz4MtContext e = { LZ4MT_RESULT_OK, 0 };

	e.result		= LZ4MT_RESULT_OK;
	e.readCtx		= nullptr;
	e.read			= nullptr;
	e.readEof		= nullptr;
	e.writeCtx		= nullptr;
	e.write			= nullptr;
	e.compress		= nullptr;
	e.compressBound	= nullptr;
	e.decompress	= nullptr;
	e.mode			= LZ4MT_MODE_PARALLEL;

	return e;
}


extern "C" Lz4MtStreamDescriptor
lz4mtInitStreamDescriptor()
{
	Lz4MtStreamDescriptor e = {{ LZ4MT_MODE_DEFAULT, 0 }};

	e.flg.presetDictionary	= 0;
	e.flg.streamChecksum	= 1;
	e.flg.reserved1			= 0;
	e.flg.streamSize		= 0;
	e.flg.blockChecksum		= 0;
	e.flg.blockIndependance	= 1;
	e.flg.versionNumber		= 1;

	e.bd.blockMaximumSize	= LZ4S_BLOCKSIZEID_DEFAULT;

	return e;
}


extern "C" const char*
lz4mtResultToString(Lz4MtResult result)
{
	const char* s = "???";
	switch(result) {
	case LZ4MT_RESULT_OK:
		s = "OK";
		break;
	case LZ4MT_RESULT_ERROR:
		s = "ERROR";
		break;
	case LZ4MT_RESULT_INVALID_MAGIC_NUMBER:
		s = "INVALID_MAGIC_NUMBER";
		break;
	case LZ4MT_RESULT_INVALID_HEADER:
		s = "INVALID_HEADER";
		break;
	case LZ4MT_RESULT_PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET:
		s = "PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET";
		break;
	case LZ4MT_RESULT_BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET:
		s = "BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET";
		break;
	case LZ4MT_RESULT_INVALID_VERSION:
		s = "INVALID_VERSION";
		break;
	case LZ4MT_RESULT_INVALID_HEADER_CHECKSUM:
		s = "INVALID_HEADER_CHECKSUM";
		break;
	case LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE:
		s = "INVALID_BLOCK_MAXIMUM_SIZE";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_HEADER:
		s = "CANNOT_WRITE_HEADER";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_EOS:
		s = "CANNOT_WRITE_EOS";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM:
		s = "CANNOT_WRITE_STREAM_CHECKSUM";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE:
		s = "CANNOT_READ_BLOCK_SIZE";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA:
		s = "CANNOT_READ_BLOCK_DATA";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM:
		s = "CANNOT_READ_BLOCK_CHECKSUM";
		break;
	case LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM:
		s = "CANNOT_READ_STREAM_CHECKSUM";
		break;
	case LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH:
		s = "STREAM_CHECKSUM_MISMATCH";
		break;
	case LZ4MT_RESULT_DECOMPRESS_FAIL:
		s = "RESULT_DECOMPRESS_FAIL";
		break;
	default:
		s = "Unknown code";
		break;
	}
	return s;
}


extern "C" Lz4MtResult
lz4mtCompress(Lz4MtContext* ctx, const Lz4MtStreamDescriptor* sd)
{
	assert(ctx);
	assert(sd);

	{
		char d[LZ4S_MAX_HEADER_SIZE] = { 0 };
		auto p = &d[0];

		const auto r = validateStreamDescriptor(sd);
		if(LZ4MT_RESULT_OK != r) {
			return setResult(ctx, r);
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
		if(writeSize != ctx->write(ctx, d, writeSize)) {
			return setResult(ctx, LZ4MT_RESULT_CANNOT_WRITE_HEADER);
		}
	}

	const auto nBlockMaximumSize = getBlockSize(sd->bd.blockMaximumSize);
	const auto nBlockSize        = 4;
	const auto nBlockCheckSum    = sd->flg.blockChecksum ? 4 : 0;
	const auto cIncompressible   = 1 << (nBlockSize * 8 - 1);
	const bool streamChecksum    = 0 != sd->flg.streamChecksum;
	const bool singleThread      = 0 != (ctx->mode & LZ4MT_MODE_SEQUENTIAL);
	const auto nConcurrency      = std::thread::hardware_concurrency();
	const auto nPool             = singleThread ? 1 : nConcurrency + 1;
	const auto launch            = singleThread ? std::launch::deferred : std::launch::async;

	Lz4Mt::MemPool srcBufferPool(nBlockMaximumSize, nPool);
	Lz4Mt::MemPool dstBufferPool(nBlockMaximumSize, nPool);
	std::vector<std::future<void>> futures;
	Lz4Mt::Xxh32 xxhStream(LZ4S_CHECKSUM_SEED);

	const auto f =
		[&futures, &dstBufferPool, &xxhStream
		 , ctx, nBlockCheckSum, streamChecksum, launch, cIncompressible
		 ]
		(int i, Lz4Mt::MemPool::Buffer* src, int srcSize)
	{
		Lz4Mt::MemPool::AutoDelete srcAutoDelete(src);
		if(error(ctx)) {
			return;
		}

		const auto* srcPtr = src->data();
		auto dst = dstBufferPool.alloc();
		Lz4Mt::MemPool::AutoDelete wrtAutoDelete(dst);
		auto* cmpPtr = dst->data();
		const auto cmpSize = ctx->compress(srcPtr, cmpPtr, srcSize, srcSize);
		const bool incompressible = (cmpSize <= 0);
		const auto* cPtr  = incompressible ? srcPtr  : cmpPtr;
		const auto  cSize = incompressible ? srcSize : cmpSize;

		std::future<uint32_t> futureBlockHash;
		if(nBlockCheckSum) {
			futureBlockHash = std::async(launch, [=] {
				return Lz4Mt::Xxh32(cPtr, cSize, LZ4S_CHECKSUM_SEED).digest();
			});
		}

		if(incompressible) {
			wrtAutoDelete.reset();
		}

		if(i > 0) {
			futures[i-1].wait();
		}

		std::future<void> futureStreamHash;
		if(streamChecksum) {
			futureStreamHash = std::async(launch, [=, &xxhStream] {
				xxhStream.update(srcPtr, srcSize);
			});
		}

		if(incompressible) {
			writeU32(ctx, cSize | cIncompressible);
			writeBin(ctx, srcPtr, srcSize);
		} else {
			writeU32(ctx, cSize);
			writeBin(ctx, cmpPtr, cmpSize);
		}

		if(futureBlockHash.valid()) {
			writeU32(ctx, futureBlockHash.get());
		}

		if(futureStreamHash.valid()) {
			futureStreamHash.wait();
		}
	};

	for(int i = 0;; ++i) {
		auto src = srcBufferPool.alloc();
		auto* srcPtr = src->data();
		const auto srcSize = src->size();
		const auto readSize = ctx->read(ctx, srcPtr, static_cast<int>(srcSize));

		if(0 == readSize) {
			break;
		}

		if(singleThread) {
			f(0, src, readSize);
		} else {
			futures.emplace_back(std::async(launch, f, i, src, readSize));
		}
	}

	for(auto& e : futures) {
		e.wait();
	}

	if(!writeU32(ctx, LZ4S_EOS)) {
		return LZ4MT_RESULT_CANNOT_WRITE_EOS;
	}

	if(streamChecksum) {
		const auto digest = xxhStream.digest();
		if(!writeU32(ctx, digest)) {
			return LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM;
		}
	}

	return LZ4MT_RESULT_OK;
}


extern "C" Lz4MtResult
lz4mtDecompress(Lz4MtContext* ctx, Lz4MtStreamDescriptor* sd)
{
	assert(ctx);
	assert(sd);

	std::atomic<bool> quit(false);

	setResult(ctx, LZ4MT_RESULT_OK);
	while(!quit && !error(ctx) && !ctx->readEof(ctx)) {
		const auto magic = readU32(ctx);
		if(error(ctx)) {
			if(ctx->readEof(ctx)) {
				setResult(ctx, LZ4MT_RESULT_OK);
			} else {
				setResult(ctx, LZ4MT_RESULT_INVALID_HEADER);
			}
			break;
		}

		if(isSkippableMagicNumber(magic)) {
			const auto size = readU32(ctx);
			if(error(ctx)) {
				setResult(ctx, LZ4MT_RESULT_INVALID_HEADER);
				break;
			}
			const auto s = ctx->readSkippable(ctx, magic, size);
			if(s < 0 || error(ctx)) {
				setResult(ctx, LZ4MT_RESULT_INVALID_HEADER);
				break;
			}
			continue;
		}

		if(LZ4S_MAGICNUMBER != magic) {
			ctx->readSeek(ctx, -4);
			setResult(ctx, LZ4MT_RESULT_INVALID_MAGIC_NUMBER);
			break;
		}

		char d[LZ4S_MAX_HEADER_SIZE] = { 0 };
		auto* p = d;
		const auto* sumBegin = p;

		if(2 != ctx->read(ctx, p, 2)) {
			setResult(ctx, LZ4MT_RESULT_INVALID_HEADER);
			break;
		}
		sd->flg = charToFlg(*p++);
		sd->bd  = charToBc(*p++);
		const auto r = validateStreamDescriptor(sd);
		if(LZ4MT_RESULT_OK != r) {
			setResult(ctx, r);
			break;
		}

		const int nExInfo =
			  (sd->flg.streamSize       ? sizeof(uint64_t) : 0)
			+ (sd->flg.presetDictionary ? sizeof(uint32_t) : 0)
			+ 1
		;
		if(nExInfo != ctx->read(ctx, p, nExInfo)) {
			setResult(ctx, LZ4MT_RESULT_INVALID_HEADER);
			break;
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
			setResult(ctx, LZ4MT_RESULT_INVALID_HEADER_CHECKSUM);
			break;
		}

		const auto nBlockMaximumSize = getBlockSize(sd->bd.blockMaximumSize);
		const auto nBlockCheckSum    = sd->flg.blockChecksum ? 4 : 0;
		const bool streamChecksum    = 0 != sd->flg.streamChecksum;
		const bool singleThread      = 0 != (ctx->mode & LZ4MT_MODE_SEQUENTIAL);
		const auto nConcurrency      = std::thread::hardware_concurrency();
		const auto nPool             = singleThread ? 1 : nConcurrency + 1;
		const auto launch            = singleThread ? std::launch::deferred : std::launch::async;

		Lz4Mt::MemPool srcBufferPool(nBlockMaximumSize, nPool);
		Lz4Mt::MemPool dstBufferPool(nBlockMaximumSize, nPool);
		std::vector<std::future<Lz4MtResult>> futures;
		Lz4Mt::Xxh32 xxhStream(LZ4S_CHECKSUM_SEED);

		const auto f = [
			&futures, &dstBufferPool, &xxhStream, &quit
			, ctx, nBlockCheckSum, streamChecksum, launch
		] (int i, Lz4Mt::MemPool::Buffer* src, bool incompressible, uint32_t blockChecksum)
		{
			Lz4Mt::MemPool::AutoDelete srcAutoDelete(src);

			if(error(ctx) || quit) {
				return LZ4MT_RESULT_OK;
			}

			const auto* srcPtr = src->data();
			const auto srcSize = static_cast<int>(src->size());

			std::future<uint32_t> futureBlockHash;
			if(nBlockCheckSum) {
				futureBlockHash = std::async(launch, [=] {
					return Lz4Mt::Xxh32(srcPtr, srcSize, LZ4S_CHECKSUM_SEED).digest();
				});
			}

			if(incompressible) {
				if(i > 0) {
					futures[i-1].wait();
				}

				std::future<void> futureStreamHash;
				if(streamChecksum) {
					futureStreamHash = std::async(
						  launch
						, [&xxhStream, srcPtr, srcSize] {
							xxhStream.update(srcPtr, srcSize);
						}
					);
				}
				writeBin(ctx, srcPtr, srcSize);
				futureStreamHash.wait();
			} else {
				auto dst = dstBufferPool.alloc();
				Lz4Mt::MemPool::AutoDelete wrtAutoDelete(dst);

				auto* dstPtr = dst->data();
				const auto dstSize = dst->size();
				const auto decSize = ctx->decompress(
					srcPtr, dstPtr, srcSize, static_cast<int>(dstSize));
				if(decSize < 0) {
					quit = true;
					return LZ4MT_RESULT_DECOMPRESS_FAIL;
				}

				if(i > 0) {
					futures[i-1].wait();
				}

				std::future<void> futureStreamHash;
				if(streamChecksum) {
					futureStreamHash = std::async(
						  launch
						, [&xxhStream, dstPtr, decSize] {
							xxhStream.update(dstPtr, decSize);
						}
					);
				}
				writeBin(ctx, dstPtr, decSize);
				futureStreamHash.wait();
			}

			if(futureBlockHash.valid()) {
				auto bh = futureBlockHash.get();
				if(bh != blockChecksum) {
					quit = true;
					return LZ4MT_RESULT_BLOCK_CHECKSUM_MISMATCH;
				}
			}

			return LZ4MT_RESULT_OK;
		};

		for(int i = 0; !quit && !ctx->readEof(ctx); ++i) {
			const auto srcBits = readU32(ctx);
			if(error(ctx)) {
				quit = true;
				setResult(ctx, LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE);
				break;
			}

			if(LZ4S_EOS == srcBits) {
				break;
			}

			const auto incompMask     = (1 << 31);
			const bool incompressible = 0 != (srcBits & incompMask);
			const auto srcSize        = static_cast<int>(srcBits & ~incompMask);

			auto src = srcBufferPool.alloc();
			const auto readSize = ctx->read(ctx, src->data(), srcSize);
			if(srcSize != readSize || error(ctx)) {
				quit = true;
				setResult(ctx, LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA);
				break;
			}
			src->resize(readSize);

			const auto blockCheckSum = nBlockCheckSum ? readU32(ctx) : 0;
			if(error(ctx)) {
				quit = true;
				setResult(ctx, LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM);
				break;
			}

			if(singleThread) {
				f(0, src, incompressible, blockCheckSum);
			} else {
				futures.emplace_back(std::async(
					  launch
					, f, i, src, incompressible, blockCheckSum
				));
			}
		}

		for(auto& e : futures) {
			const auto r = e.get();
			if(LZ4MT_RESULT_OK != r) {
				setResult(ctx, r);
			}
		}

		if(!error(ctx) && streamChecksum) {
			const auto srcStreamChecksum = readU32(ctx);
			if(error(ctx)) {
				setResult(ctx, LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM);
				break;
			}
			if(xxhStream.digest() != srcStreamChecksum) {
				setResult(ctx, LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH);
				break;
			}
		}
	}

	return ctx->result;
}
