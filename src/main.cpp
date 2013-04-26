#include <cassert>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "lz4.h"
#include "lz4hc.h"
#include "xxhash.h"
#include "lz4mt.h"
#include "lz4mt_benchmark.h"

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
		if(fp != stdin && fp != stdout) {
			::fclose(fp);
		}
	}
}

FILE* getStdin() {
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
#endif
	return stdin;
}

FILE* getStdout() {
#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	return stdout;
}

bool fileExist(const std::string& filename) {
	if("stdin" == filename || "stdout" == filename) {
		return false;
	} else {
		FILE* fp = fopen_(filename.c_str(), "rb");
		fclose_(fp);
		return nullptr != fp;
	}
}

FILE* readCtx(const Lz4MtContext* ctx) {
	return reinterpret_cast<FILE*>(ctx->readCtx);
}

FILE* writeCtx(const Lz4MtContext* ctx) {
	return reinterpret_cast<FILE*>(ctx->writeCtx);
}

bool openIstream(Lz4MtContext* ctx, const std::string& filename) {
	FILE* fp = nullptr;
	if("stdin" == filename) {
		fp = getStdin();
	} else {
		fp = fopen_(filename.c_str(), "rb");
	}
	ctx->readCtx = reinterpret_cast<void*>(fp);
	return nullptr != fp;
}

bool openOstream(Lz4MtContext* ctx, const std::string& filename) {
	FILE* fp = nullptr;
	if("stdout" == filename) {
		fp = getStdout();
	} else {
		fp = fopen_(filename.c_str(), "wb");
	}
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

uint64_t getFilesize(const std::string& fileanme) {
	int r = 0;
#if defined(_MSC_VER)
	struct _stat64 s = { 0 };
	r = _stat64(fileanme.c_str(), &s);
	auto S_ISREG = [](decltype(s.st_mode) x) {
		return (x & S_IFMT) == S_IFREG;
	};
#else
	struct stat s = { 0 };
	r = _stat64(fileanme.c_str(), &s);
#endif
	if(r || !S_ISREG(s.st_mode)) {
		return 0;
	} else {
		return static_cast<uint64_t>(s.st_size);
	}
}

} // namespace Cstdio


namespace {

const char* LZ4MT_EXTENSION = ".lz4";

const char* usage = 
	"usage :\n"
	"  lz4mt [switch...] <input> [output]\n"
	"switch :\n"
	"  -c0/-c  : Compress (lz4) (default)\n"
	"  -c1/-hc : Compress (lz4hc)\n"
	"  -d      : Decompress\n"
	"  -y      : Overwrite without prompting\n"
	"  -s      : Single thread mode\n"
	"  -m      : Multi thread mode (default)\n"
	"  -H      : Help (this text + advanced options)\n"
	"  input   : can be 'stdin' (pipe) or a filename\n"
	"  output  : can be 'stdout'(pipe) or a filename\n"// "or 'null'\n"
;

const char* usage_advanced =
	"\nAdvanced options :\n"
//	" -t       : test compressed file \n"
	" -B#      : Block size [4-7](default : 7)\n"
	" -x       : enable block checksum (default:disabled)\n"
	" -nx      : disable stream checksum (default:enabled)\n"
	" -b#      : benchmark files, using # [0-1] compression level\n"
	" -i#      : iteration loops [1-9](default : 3), benchmark mode only\n"
;

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
	bool overwrite = false;
	Lz4Mt::Benchmark benchmark;

	map<string, function<void ()>> opts;
	opts["-c0"] =
	opts["-c" ] = [&] { compMode = CompMode::COMPRESS_C0; };
	opts["-c1"] =
	opts["-hc"] = [&] { compMode = CompMode::COMPRESS_C1; };
	opts["-d" ] = [&] { compMode = CompMode::DECOMPRESS; };
	opts["-y" ] = [&] { overwrite = true; };
	opts["-s" ] = [&] { mode |= LZ4MT_MODE_SEQUENTIAL; };
	opts["-m" ] = [&] { mode &= ~LZ4MT_MODE_SEQUENTIAL; };
	opts["--help"] = opts["-h" ] = opts["/?" ] = [&] {
		cerr << usage;
		exit(EXIT_FAILURE);
	};
	opts["-H" ] = [&] {
		cerr << usage << usage_advanced;
		exit(EXIT_FAILURE);
	};
	for(int i = 4; i <= 7; ++i) {
		opts[string("-B") + to_string(i)] = [&, i] {
			sd.bd.blockMaximumSize = static_cast<char>(i);
		};
	}
	opts["-x" ] = [&] { sd.flg.blockChecksum = 1; };
	opts["-nx"] = [&] { sd.flg.streamChecksum = 0; };
	for(int i = 0; i <= 1; ++i) {
		opts["-b" + to_string(i)] = [&, i] {
			if(i == 0) {
				compMode = CompMode::COMPRESS_C0;
			} else {
				compMode = CompMode::COMPRESS_C1;
			}
			benchmark.enable = true;
		};
	}
	for(int i = 1; i <= 9; ++i) {
		opts[string("-i") + to_string(i)] = [&, i] {
			benchmark.nIter = i;
			benchmark.enable = true;
		};
	}

	for(int iarg = 1; iarg < argc; ++iarg) {
		const auto a = string(argv[iarg]);
		const auto i = opts.find(a);
		if(opts.end() != i) {
			i->second();
		} else if(a[0] == '-') {
			cerr << "ERROR: bad switch [" << a << "]\n";
			exit(EXIT_FAILURE);
		} else if(benchmark.enable) {
			benchmark.files.push_back(a);
		} else if(inpFilename.empty()) {
			inpFilename = a;
		} else if(outFilename.empty()) {
			outFilename = a;
		} else {
			cerr << "ERROR: Bad argument [" << a << "]\n";
			exit(EXIT_FAILURE);
		}
	}

	Lz4MtContext ctx = lz4mtInitContext();
	ctx.mode	 = static_cast<Lz4MtMode>(mode);
	ctx.read	 = read;
	ctx.readSeek = readSeek;
	ctx.readEof	 = readEof;
	ctx.write	 = write;
	if(CompMode::COMPRESS_C1 == compMode) {
		ctx.compress = LZ4_compressHC_limitedOutput;
	}

	if(benchmark.enable) {
		benchmark.openIstream	= openIstream;
		benchmark.closeIstream	= closeIstream;
		benchmark.getFilesize	= getFilesize;
		benchmark.measure(ctx, sd);
		exit(EXIT_SUCCESS);
	}

	if(inpFilename.empty()) {
		cerr << "ERROR: No input filename\n";
		exit(EXIT_FAILURE);
	}

	if(outFilename.empty()) {
		if(   CompMode::COMPRESS_C0 == compMode
		   || CompMode::COMPRESS_C1 == compMode
		) {
			if("stdin" == inpFilename) {
				outFilename = "stdout";
			} else {
				outFilename = inpFilename + LZ4MT_EXTENSION;
			}
		} else {
			cerr << "ERROR: No output filename\n";
			exit(EXIT_FAILURE);
		}
	}

	if(!openIstream(&ctx, inpFilename)) {
		cerr << "ERROR: Can't open input file [" << inpFilename << "]\n";
		exit(EXIT_FAILURE);
	}

	if(!overwrite && fileExist(outFilename)) {
		int ch = 0;
		if("stdin" != inpFilename) {
			cerr << "Overwrite [y/N]? ";
			ch = cin.get();
		}
		if(ch != 'y') {
			cerr << "Abort: " << outFilename << " already exists\n";
			exit(EXIT_FAILURE);
		}
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
	case CompMode::COMPRESS_C1:
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
