#include <cassert>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "lz4.h"
#include "lz4hc.h"
#include "lz4mt.h"


namespace Cstdio {

FILE* fopen_(const char* filename, const char* mode) {
#if defined(_MSC_VER)
	FILE* fp = nullptr;
	::fopen_s(&fp, filename, mode);
	return fp;
#else
	return ::fopen(filename, mode);
#endif
}

void fclose_(FILE* fp) {
	if(fp) {
		::fclose(fp);
	}
}

FILE* readCtx(const Lz4MtContext* ctx) {
	return reinterpret_cast<FILE*>(ctx->readCtx);
}

FILE* writeCtx(const Lz4MtContext* ctx) {
	return reinterpret_cast<FILE*>(ctx->writeCtx);
}

bool openIstream(Lz4MtContext* ctx, const std::string& filename) {
	auto* fp = fopen_(filename.c_str(), "rb");
	ctx->readCtx = reinterpret_cast<void*>(fp);
	return nullptr != fp;
}

bool openOstream(Lz4MtContext* ctx, const std::string& filename) {
	auto* fp = fopen_(filename.c_str(), "wb");
	ctx->writeCtx = reinterpret_cast<void*>(fp);
	return nullptr != fp;
}

void closeIstream(Lz4MtContext* ctx) {
	fclose_(readCtx(ctx));
	ctx->readCtx = nullptr;
}

void closeOstream(Lz4MtContext* ctx) {
	fclose_(writeCtx(ctx));
	ctx->writeCtx = nullptr;
}

int read(const Lz4MtContext* ctx, void* dest, int destSize) {
	if(auto* fp = readCtx(ctx)) {
		return static_cast<int>(::fread(dest, 1, destSize, fp));
	} else {
		return 0;
	}
}

int readSkippable(const Lz4MtContext* ctx, uint32_t /*magicNumber*/, size_t size) {
	if(auto* fp = readCtx(ctx)) {
		return ::fseek(fp, static_cast<long>(size), SEEK_CUR);
	} else {
		return -1;
	}
}

int readSeek(const Lz4MtContext* ctx, int offset) {
	if(auto* fp = readCtx(ctx)) {
		return ::fseek(fp, offset, SEEK_CUR);
	} else {
		return -1;
	}
}

int readEof(const Lz4MtContext* ctx) {
	if(auto* fp = readCtx(ctx)) {
		return ::feof(fp);
	} else {
		return 1;
	}
}

int write(const Lz4MtContext* ctx, const void* source, int sourceSize) {
	if(auto* fp = writeCtx(ctx)) {
		return static_cast<int>(::fwrite(source, 1, sourceSize, fp));
	} else {
		return 0;
	}
}

} // namespace Cstdio


namespace {

const char* LZ4MT_EXTENSION = ".lz4";

void usage() {
	std::cerr <<
		"usage :\n"
		"  lz4_mt [switch...] <input> <output>\n"
		"switch :\n"
		"  -c0/-c  : Compress (lz4) (default)\n"
		"  -c1/-hc : Compress (lz4hc)\n"
		"  -d      : Decompress\n"
//		"  -y      : Overwrite without prompting\n"
		"  -s      : Single thread mode\n"
		"  -m      : Multi thread mode (default)\n"
		"  -h      : help\n"
//		"input     : can be 'stdin' (pipe) or a filename\n"
//		"output    : can be 'stdout'(pipe) or a filename or 'null'\n"
		"\n"
//		" -t       : test compressed file \n"
		" -B#      : Block size [4-7](default : 7)\n"
		" -x       : enable block checksum (default:disabled)\n"
		" -nx      : disable stream checksum (default:enabled)\n"
//		" -b#      : benchmark files, using # [0-1] compression level\n"
//		" -i#      : iteration loops [1-9](default : 3), benchmark mode only\n"
	;
}

} // anonymous namespace


int main(int argc, char* argv[]) {
	using namespace Cstdio;
	using namespace std;

	enum class CompMode {
		  DECOMPRESS
		, COMPRESS_C0
		, COMPRESS_C1
	} compMode = CompMode::COMPRESS_C0;

	Lz4MtStreamDescriptor sd = lz4mtInitStreamDescriptor();
	int mode = LZ4MT_MODE_DEFAULT;
	string inpFilename;
	string outFilename;

	map<string, function<void ()>> opts;
	opts["-c0"] =
	opts["-c" ] = [&] { compMode = CompMode::COMPRESS_C0; };
	opts["-c1"] =
	opts["-hc"] = [&] { compMode = CompMode::COMPRESS_C1; };
	opts["-d" ] = [&] { compMode = CompMode::DECOMPRESS; };
	opts["-s" ] = [&] { mode |= LZ4MT_MODE_SEQUENTIAL; };
	opts["-m" ] = [&] { mode &= ~LZ4MT_MODE_SEQUENTIAL; };
	opts["-B4"] = [&] { sd.bd.blockMaximumSize = 4; };
	opts["-B5"] = [&] { sd.bd.blockMaximumSize = 5; };
	opts["-B6"] = [&] { sd.bd.blockMaximumSize = 6; };
	opts["-B7"] = [&] { sd.bd.blockMaximumSize = 7; };
	opts["-x" ] = [&] { sd.flg.blockChecksum = 1; };
	opts["-nx"] = [&] { sd.flg.streamChecksum = 0; };
	opts["--help"] = opts["-h" ] = opts["-H" ] =
	opts["/?" ] = [&] { usage(); exit(EXIT_FAILURE); };

	for(int iarg = 1; iarg < argc; ++iarg) {
		const auto a = string(argv[iarg]);
		const auto i = opts.find(a);
		if(opts.end() != i) {
			i->second();
		} else if(a[0] == '-') {
			cerr << "ERROR: bad switch [" << a << "]\n";
			exit(EXIT_FAILURE);
		} else if(inpFilename.empty()) {
			inpFilename = a;
		} else if(outFilename.empty()) {
			outFilename = a;
		} else {
			cerr << "ERROR: Bad argument [" << a << "]\n";
			exit(EXIT_FAILURE);
		}
	}

	if(inpFilename.empty()) {
		cerr << "ERROR: No input filename\n";
		exit(EXIT_FAILURE);
	}

	if(outFilename.empty()) {
		if(   CompMode::COMPRESS_C0 == compMode
		   || CompMode::COMPRESS_C1 == compMode
		) {
			outFilename = inpFilename + LZ4MT_EXTENSION;
		} else {
			cerr << "ERROR: No output filename\n";
			exit(EXIT_FAILURE);
		}
	}

	Lz4MtContext ctx = lz4mtInitContext();
	ctx.mode	 = static_cast<Lz4MtMode>(mode);
	ctx.read	 = read;
	ctx.readSeek = readSeek;
	ctx.readEof	 = readEof;
	ctx.write	 = write;

	if(!openIstream(&ctx, inpFilename)) {
		cerr << "ERROR: Can't open input file [" << inpFilename << "]\n";
		exit(EXIT_FAILURE);
	}

	if(!openOstream(&ctx, outFilename)) {
		cerr << "ERROR: Can't open output file [" << outFilename << "]\n";
		exit(EXIT_FAILURE);
	}

	auto e = LZ4MT_RESULT_OK;
	switch(compMode) {
	default:
		assert(0);
		cerr << "ERROR: You must specify a switch -c or -d\n";
		exit(EXIT_FAILURE);
		break;

	case CompMode::DECOMPRESS:
		e = lz4mtDecompress(&ctx, &sd);
		break;

	case CompMode::COMPRESS_C0:
		e = lz4mtCompress(&ctx, &sd);
		break;

	case CompMode::COMPRESS_C1:
		ctx.compress = LZ4_compressHC_limitedOutput;
		e = lz4mtCompress(&ctx, &sd);
		break;
	}

	closeOstream(&ctx);
	closeIstream(&ctx);

	if(LZ4MT_RESULT_OK != e) {
		cerr << "ERROR: " << lz4mtResultToString(e) << "\n";
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
