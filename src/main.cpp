#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "lz4.h"
#include "lz4hc.h"
#include "xxhash.h"
#include "lz4mt.h"
#include "lz4mt_benchmark.h"
#include "lz4mt_io_cstdio.h"
#include "test_clock.h"


namespace {

const char LZ4MT_EXTENSION[] = ".lz4";

const char usage[] = 
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

const char usage_advanced[] =
	"\nAdvanced options :\n"
//	" -t       : test compressed file \n"
	" -B#      : Block size [4-7](default : 7)\n"
	" -x       : enable block checksum (default:disabled)\n"
	" -nx      : disable stream checksum (default:enabled)\n"
	" -b#      : benchmark files, using # [0-1] compression level\n"
	" -i#      : iteration loops [1-9](default : 3), benchmark mode"
	             " only\n"
;

struct Option {
	Option(int argc, char* argv[])
		: error(false)
		, compMode(CompMode::COMPRESS_C0)
		, sd(lz4mtInitStreamDescriptor())
		, mode(LZ4MT_MODE_DEFAULT)
		, overwrite(false)
	{
		std::map<std::string, std::function<void ()>> opts;
		opts["-c0"] =
		opts["-c" ] = [&] { compMode = CompMode::COMPRESS_C0; };
		opts["-c1"] =
		opts["-hc"] = [&] { compMode = CompMode::COMPRESS_C1; };
		opts["-d" ] = [&] { compMode = CompMode::DECOMPRESS; };
		opts["-y" ] = [&] { overwrite = true; };
		opts["-s" ] = [&] { mode |= LZ4MT_MODE_SEQUENTIAL; };
		opts["-m" ] = [&] { mode &= ~LZ4MT_MODE_SEQUENTIAL; };
		opts["--help"] = opts["-h" ] = opts["/?" ] = [&] {
			std::cerr << usage;
			error = true;
		};
		opts["-H" ] = [&] {
			std::cerr << usage << usage_advanced;
			error = true;
		};
		for(int i = 4; i <= 7; ++i) {
			opts[std::string("-B") + std::to_string(i)] = [&, i] {
				sd.bd.blockMaximumSize = static_cast<char>(i);
			};
		}
		opts["-x" ] = [&] { sd.flg.blockChecksum = 1; };
		opts["-nx"] = [&] { sd.flg.streamChecksum = 0; };
		for(int i = 0; i <= 1; ++i) {
			opts["-b" + std::to_string(i)] = [&, i] {
				if(i == 0) {
					compMode = CompMode::COMPRESS_C0;
				} else {
					compMode = CompMode::COMPRESS_C1;
				}
				benchmark.enable = true;
			};
		}
		for(int i = 1; i <= 9; ++i) {
			opts[std::string("-i") + std::to_string(i)] = [&, i] {
				benchmark.nIter = i;
				benchmark.enable = true;
			};
		}

		for(int iarg = 1; iarg < argc && !error; ++iarg) {
			const auto a = std::string(argv[iarg]);
			const auto i = opts.find(a);
			if(opts.end() != i) {
				i->second();
			} else if(a[0] == '-') {
				std::cerr << "ERROR: bad switch [" << a << "]\n";
				error = true;
			} else if(benchmark.enable) {
				benchmark.files.push_back(a);
			} else if(inpFilename.empty()) {
				inpFilename = a;
			} else if(outFilename.empty()) {
				outFilename = a;
			} else {
				std::cerr << "ERROR: Bad argument [" << a << "]\n";
				error = true;
			}
		}
	}

	bool isCompress() const {
		return CompMode::COMPRESS_C0 == compMode
		    || CompMode::COMPRESS_C1 == compMode;
	}

	bool isDecompress() const {
		return CompMode::DECOMPRESS == compMode;
	}

	enum class CompMode {
		  DECOMPRESS
		, COMPRESS_C0
		, COMPRESS_C1
	};

	bool error;
	CompMode compMode;
	Lz4MtStreamDescriptor sd;
	int mode;
	std::string inpFilename;
	std::string outFilename;
	bool overwrite;
	Lz4Mt::Benchmark benchmark;
};

} // anonymous namespace



int main(int argc, char* argv[]) {
	using namespace Lz4Mt::Cstdio;
	Option opt(argc, argv);

	Lz4MtContext ctx = lz4mtInitContext();
	ctx.mode			= static_cast<Lz4MtMode>(opt.mode);
	ctx.read			= read;
	ctx.readSeek		= readSeek;
	ctx.readEof			= readEof;
	ctx.write			= write;
	ctx.compress		= LZ4_compress_limitedOutput;
	ctx.compressBound	= LZ4_compressBound;
	ctx.decompress		= LZ4_decompress_safe;
	if(Option::CompMode::COMPRESS_C1 == opt.compMode) {
		ctx.compress = LZ4_compressHC_limitedOutput;
	}

	if(opt.benchmark.enable) {
		opt.benchmark.openIstream	= openIstream;
		opt.benchmark.closeIstream	= closeIstream;
		opt.benchmark.getFilesize	= getFilesize;
		opt.benchmark.measure(ctx, opt.sd);
		exit(EXIT_SUCCESS);
	}

	if(opt.inpFilename.empty()) {
		std::cerr << "ERROR: No input filename\n";
		exit(EXIT_FAILURE);
	}

	if(opt.outFilename.empty()) {
		if(opt.isCompress()) {
			if("stdin" == opt.inpFilename) {
				opt.outFilename = "stdout";
			} else {
				opt.outFilename = opt.inpFilename + LZ4MT_EXTENSION;
			}
		} else {
			std::cerr << "ERROR: No output filename\n";
			exit(EXIT_FAILURE);
		}
	}

	if(!openIstream(&ctx, opt.inpFilename)) {
		std::cerr << "ERROR: Can't open input file "
				  << "[" << opt.inpFilename << "]\n";
		exit(EXIT_FAILURE);
	}

	if(!opt.overwrite && fileExist(opt.outFilename)) {
		const int ch = [&]() -> int {
			if("stdin" != opt.inpFilename) {
				std::cerr << "Overwrite [y/N]? ";
				return std::cin.get();
			} else {
				return 0;
			}
		} ();
		if('y' != ch && 'Y' != ch) {
			std::cerr << "Abort: " << opt.outFilename << " already exists\n";
			exit(EXIT_FAILURE);
		}
	}

	if(!openOstream(&ctx, opt.outFilename)) {
		std::cerr << "ERROR: Can't open output file ["
				  << opt.outFilename << "]\n";
		exit(EXIT_FAILURE);
	}

	const auto t0 = Clock::now();
	const auto e = [&]() -> Lz4MtResult {
		if(opt.isCompress()) {
			return lz4mtCompress(&ctx, &opt.sd);
		} else if(opt.isDecompress()) {
			return lz4mtDecompress(&ctx, &opt.sd);
		} else {
			assert(0);
			std::cerr << "ERROR: You must specify a switch -c or -d\n";
			return LZ4MT_RESULT_BAD_ARG;
		}
	} ();
	const auto t1 = Clock::now();

	closeOstream(&ctx);
	closeIstream(&ctx);

	const auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
	std::cerr << "Total time: " << dt << "sec\n";

	if(LZ4MT_RESULT_OK != e) {
		std::cerr << "ERROR: " << lz4mtResultToString(e) << "\n";
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
