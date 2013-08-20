#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
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
	"  -H      : Help (this text + advanced options)\n"
;

const char usage_advanced[] =
	"\nAdvanced options :\n"
	" -t       : decode test mode (do not output anything)\n"
	" -B#      : Block size [4-7](default : 7)\n"
	" -BX      : enable block checksum (default:disabled)\n"
	" -Sx      : disable stream checksum (default:enabled)\n"
	" -b#      : benchmark files, using # [0-1] compression level\n"
	" -i#      : iteration loops [1-9](default : 3), benchmark mode"
	             " only\n"
	"  -s      : Single thread mode\n"
	"  -m      : Multi thread mode (default)\n"
	"  input   : can be 'stdin' (pipe) or a filename\n"
	"  output  : can be 'stdout'(pipe) or a filename or 'null'\n"
;

struct Option {
	Option(int argc, char* argv[])
		: error(false)
		, exitFlag(false)
		, compMode(CompMode::COMPRESS_C0)
		, sd(lz4mtInitStreamDescriptor())
		, mode(LZ4MT_MODE_DEFAULT)
		, inpFilename()
		, outFilename()
		, nullWrite(false)
		, overwrite(false)
		, benchmark()
	{
		static const std::string nullFilename = "null";

		for(int iarg = 1; iarg < argc && !error && !exitFlag; ++iarg) {
			const auto a = argv[iarg];
			const auto a0 = a[0];
			const auto a1 = a[1];

			if(0 == a0) {
				continue;
			} else if('-' != a0) {
				if(benchmark.enable) {
					benchmark.files.push_back(a);
				} else if(inpFilename.empty()) {
					inpFilename = a;
				} else if(outFilename.empty()) {
					outFilename = a;
				} else {
					std::cerr << "ERROR: Bad argument [" << a << "]\n";
					error = true;
				}
			} else if('-' == a0 && 0 == a1) {
				if(inpFilename.empty()) {
					inpFilename = "stdin";
				} else {
					outFilename = "stdout";
				}
			} else {
				for(int i = 1; 0 != a[i] && !error && !exitFlag;) {
					const auto getif = [&] (char c0) {
						const auto x0 = a[i];
						if(x0 == c0) {
							++i;
							return true;
						} else {
							return false;
						}
					};

					const auto getif2 = [&] (char c0, char c1) {
						const auto x0 = a[i];
						const auto x1 = x0 ? a[i+1] : x0;
						if(x0 == c0 && x1 == c1) {
							i += 2;
							return true;
						} else {
							return false;
						}
					};

					if(getif('H')) {						// -H
						showUsage(true);
						exitFlag = true;
					} else if(getif2('c', '0')) {			// -c0
						compMode = CompMode::COMPRESS_C0;
					} else if(getif2('c', '1')) {			// -c1
						compMode = CompMode::COMPRESS_C1;
					} else if(getif('c')) {					// -c?
						// NOTE: no bad usage
					} else if(getif2('h', 'c')) {			// -hc
						compMode = CompMode::COMPRESS_C1;
					} else if(getif('h')) {					// -h?
						showUsage(true);
						exitFlag = true;
					} else if(getif('d')) {					// -d
						compMode = CompMode::DECOMPRESS;
					} else if(getif('t')) {					// -t
						compMode = CompMode::DECOMPRESS;
						outFilename = nullFilename;
					} else if(getif('B')) {
						for(;;) {
							if(getif('4')) {				// -B4
								sd.bd.blockMaximumSize = 4;
							} else if(getif('5')) {			// -B5
								sd.bd.blockMaximumSize = 5;
							} else if(getif('6')) {			// -B6
								sd.bd.blockMaximumSize = 6;
							} else if(getif('7')) {			// -B7
								sd.bd.blockMaximumSize = 7;
//							} else if(getif('D')) {			// -BD
//								// TODO : Implement
							} else if(getif('X')) {			// -BX
								sd.flg.blockChecksum = 1;
							} else {						// -B?
								// NOTE: no bad usage
								break;
							}
						}
					} else if(getif2('S', 'x')) {			// -Sx
						sd.flg.streamChecksum = 0;
					} else if(getif('S')) {					// -S?
						showBadUsage(a[i], a[i+1]);
						error = true;
					} else if(getif2('b', '0')) {			// -b0
						compMode = CompMode::COMPRESS_C0;
						benchmark.enable = true;
					} else if(getif2('b', '1')) {			// -b1
						compMode = CompMode::COMPRESS_C1;
						benchmark.enable = true;
					} else if(getif('b')) {					// -b?
						// NOTE: no bad usage
					} else if(getif('i')) {
						for(char x = '1'; x <= '9'; ++x) {	// -i[1-9]
							if(getif(x)) {
								benchmark.nIter = x - '0';
								benchmark.enable = true;
								break;
							}
						}
						// NOTE: no bad usage
					} else if(getif('y')) {					// -y
						overwrite = true;
//					} else if(getif('p')) {					// -p
//						// Pause at the end (benchmark only)
//						// (hidden option)
//					} else if(getif('v')) {					// -v
//						// Verbose mode
//					} else if(getif('l')) {					// -l
//						// Use Legacy format (hidden option)
					} else {
						// Unrecognised command
						showBadUsage(a[i]);
						error = true;
					}
				}
			}
		}

		if(cmpFilename(nullFilename, outFilename)) {
			nullWrite = true;
		}
	}

	bool isCompress() const {
		return CompMode::COMPRESS_C0 == compMode
		    || CompMode::COMPRESS_C1 == compMode;
	}

	bool isDecompress() const {
		return CompMode::DECOMPRESS == compMode;
	}

	static bool cmpFilename(const std::string& lhs, const std::string& rhs) {
		const auto pLhs = lhs.c_str();
		const auto pRhs = rhs.c_str();
#if defined(_WIN32)
		return 0 == _stricmp(pLhs, pRhs);
#else
		return 0 == strcmp(pLhs, pRhs);
#endif
	}

	static void showUsage(bool advanced = false) {
		std::cerr << usage;
		if(advanced) {
			std::cerr << usage_advanced;
		}
	}

	static void showBadUsage(char c0 = 0, char c1 = 0) {
		std::cerr << "Wrong parameters";
		if(c0 || c1) {
			std::cerr << "'";
			if(c0) {
				std::cerr << c0;
			}
			if(c1) {
				std::cerr << c1;
			}
			std::cerr << "'";
		}
		showUsage(false);
	}

	enum class CompMode {
		  DECOMPRESS
		, COMPRESS_C0
		, COMPRESS_C1
	};

	bool error;
	bool exitFlag;
	CompMode compMode;
	Lz4MtStreamDescriptor sd;
	int mode;
	std::string inpFilename;
	std::string outFilename;
	bool nullWrite;
	bool overwrite;
	Lz4Mt::Benchmark benchmark;
};

} // anonymous namespace



int main(int argc, char* argv[]) {
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	using namespace Lz4Mt::Cstdio;
	Option opt(argc, argv);

	if(opt.exitFlag) {
		exit(EXIT_SUCCESS);
	} else if(opt.error) {
		exit(EXIT_FAILURE);
	}

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

	if(!opt.nullWrite && !opt.overwrite && fileExist(opt.outFilename)) {
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

	if(!openOstream(&ctx, opt.outFilename, opt.nullWrite)) {
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

#if defined(_MSC_VER) && defined(_DEBUG)
	return EXIT_SUCCESS;
#else
	exit(EXIT_SUCCESS);
#endif
}
