#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include <algorithm>
#include <deque>
#include <cctype>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include "lz4.h"
#include "lz4hc.h"
#include "lz4mt.h"
#include "lz4mt_benchmark.h"
#include "lz4mt_io_cstdio.h"


namespace {

const char LZ4MT_EXTENSION[] = ".lz4";
const char LZ4MT_UNLZ4[] = "unlz4";
const char LZ4MT_LZ4CAT[] = "lz4cat";

const char usage[] = 
	"usage :\n"
	"  lz4mt [switch...] [input] [output]\n"
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
	"  input   : can be 'stdin' (pipe) or a filename\n"
	"  output  : can be 'stdout'(pipe) or a filename or 'null'\n"

	"\nlz4mt exclusive options :\n"
	" --lz4mt-thread 0 : Multi thread mode (default)\n"
	" --lz4mt-thread 1 : Single thread mode\n"
;

typedef std::function<bool(void)> AttyFunc;
typedef std::function<bool(const std::string&, const std::string&)> CmpFunc;

struct Option {
	Option(
		  int argc
		, char* argv[]
		, std::string stdinFilename
		, std::string stdoutFilename
		, std::string nullFilename
		, AttyFunc isAttyStdout
		, AttyFunc isAttyStdin
		, CmpFunc cmpFilename
	)
		: error(false)
		, exitFlag(false)
		, compMode(CompMode::COMPRESS_C0)
		, sd(lz4mtInitStreamDescriptor())
		, mode(LZ4MT_MODE_DEFAULT)
		, inpFilename()
		, outFilename()
		, nullWrite(false)
		, overwrite(false)
		, silence(false)
		, benchmark()
		, errorString()
	{
		if(strstr(argv[0], LZ4MT_UNLZ4)) {
			silence = true;
			compMode = CompMode::DECOMPRESS;
		} else if(strstr(argv[0], LZ4MT_LZ4CAT)) {
			silence = true;
			compMode = CompMode::DECOMPRESS;
			outFilename = stdoutFilename;
		}

		std::deque<std::string> args;
		for(int iarg = 1; iarg < argc; ++iarg) {
			args.push_back(argv[iarg]);
		}

		auto hasArg = [&]() {
			return !args.empty();
		};

		auto getArg = [&]() -> std::string {
			if(hasArg()) {
				auto a = args.front();
				args.pop_front();
				return a;
			} else {
				return std::string("");
			}
		};

		auto isDigits = [](const std::string& s) {
			return std::all_of(std::begin(s), std::end(s)
							   , static_cast<int(*)(int)>(std::isdigit));
		};

		std::map<std::string, std::function<bool ()>> opts;
		opts["--lz4mt-thread"] = [&]() -> bool {
			auto a = getArg();
			if(isDigits(a)) {
				const auto v = atoi(a.c_str());
				switch(v) {
				default:
				case 0:
					mode &= ~LZ4MT_MODE_SEQUENTIAL;
					break;
				case 1:
					mode |= LZ4MT_MODE_SEQUENTIAL;
					break;
				}
				return true;
			} else {
				errorString += "lz4mt: Bad argument for --lz4mt-thread ["
							   + std::string(a) + "]\n";
				return false;
			}
		};

		while(hasArg() && !error && !exitFlag) {
			const auto a = getArg();
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
					errorString += "lz4mt: Bad argument ["
								   + std::string(a) + "]\n";
					error = true;
				}
			} else if('-' == a0 && 0 == a1) {
				if(inpFilename.empty()) {
					inpFilename = stdinFilename;
				} else {
					outFilename = stdoutFilename;
				}
			} else if('-' == a0 && '-' == a1) {
				//	long option
				const auto it = opts.find(a);
				if(opts.end() == it) {
					errorString += "lz4mt: Bad argument ["
								   + std::string(a) + "]\n";
					error = true;
				} else {
					const auto b = it->second();
					if(!b) {
						error = true;
					}
				}
			} else {
				for(int i = 1; 0 != a[i] && !error && !exitFlag;) {
					const auto getif = [&] (char c0) -> bool {
						const auto x0 = a[i];
						if(x0 == c0) {
							++i;
							return true;
						} else {
							return false;
						}
					};

					const auto getif2 = [&] (char c0, char c1) -> bool {
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

		if(!error && !exitFlag) {
			if(inpFilename.empty()) {
				inpFilename = stdinFilename;
			}

			if(outFilename.empty()) {
				if(cmpFilename(stdinFilename, inpFilename)) {
					outFilename = stdoutFilename;
					silence = true;
				} else if(isCompress()) {
					outFilename = inpFilename + LZ4MT_EXTENSION;
				} else {
					const auto o = inpFilename.find_last_of('.');
					if(   std::string::npos != o
					   && 0 == inpFilename.compare(
						   o, strlen(LZ4MT_EXTENSION), LZ4MT_EXTENSION)
					) {
						outFilename = inpFilename.substr(0, o);
					} else {
						errorString += "lz4mt: Cannot automatically decide an output filename\n";
						error = true;
					}
				}
			}

			if(cmpFilename(nullFilename, outFilename)) {
				nullWrite = true;
			}
		}

		if(!error && !exitFlag) {
			if(   cmpFilename(stdinFilename, inpFilename)
			   && cmpFilename(stdoutFilename, outFilename)
			) {
				silence = true;
			}
		}

		if(!error && !exitFlag) {
			if(   (   isCompress()
				   && cmpFilename(stdoutFilename, outFilename)
				   && isAttyStdout()
				  )
			   || (   !isCompress()
				   && cmpFilename(stdinFilename, inpFilename)
				   && isAttyStdin()
				  )
			) {
				silence = false;
				showBadUsage();
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

	void showUsage(bool advanced = false) {
		errorString += usage;
		if(advanced) {
			errorString += usage_advanced;
		}
	}

	void showBadUsage(char c0 = 0, char c1 = 0) {
		if(c0 || c1) {
			errorString += "Wrong parameters '";
			if(c0) {
				errorString +=  c0;
			}
			if(c1) {
				errorString +=  c1;
			}
			errorString +=  "'\n";
		}
		showUsage(false);
	}

	void display(const std::string& message) {
		if(!silence) {
			std::cerr << message;
		}
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
	bool silence;
	Lz4Mt::Benchmark benchmark;
	std::string errorString;
};

} // anonymous namespace



int main(int argc, char* argv[]) {
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	using namespace Lz4Mt::Cstdio;
	Option opt(argc, argv
			   , getStdinFilename()
			   , getStdoutFilename()
			   , getNullFilename()
			   , isAttyStdout
			   , isAttyStdin
			   , compareFilename
	);

	if(opt.exitFlag) {
		opt.display(opt.errorString);
		exit(EXIT_SUCCESS);
	} else if(opt.error) {
		opt.display(opt.errorString);
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

	if(!openIstream(&ctx, opt.inpFilename)) {
		opt.display("lz4mt: Can't open input file ["
					+ opt.inpFilename + "]\n");
		exit(EXIT_FAILURE);
	}

	if(!opt.nullWrite && !opt.overwrite && fileExist(opt.outFilename)) {
		const int ch = [&]() -> int {
			if(!opt.silence) {
				opt.display("lz4mt: Overwrite [y/N]? ");
				return std::cin.get();
			} else {
				return 0;
			}
		} ();
		if('y' != ch && 'Y' != ch) {
			opt.display("lz4mt: " + opt.outFilename + " already exists\n");
			exit(EXIT_FAILURE);
		}
	}

	if(!openOstream(&ctx, opt.outFilename, opt.nullWrite)) {
		opt.display("lz4mt: Can't open output file ["
					+ opt.outFilename + "]\n");
		exit(EXIT_FAILURE);
	}

	const auto e = [&]() -> Lz4MtResult {
		if(opt.isCompress()) {
			return lz4mtCompress(&ctx, &opt.sd);
		} else if(opt.isDecompress()) {
			return lz4mtDecompress(&ctx, &opt.sd);
		} else {
			opt.display("lz4mt: You must specify a switch -c or -d\n");
			return LZ4MT_RESULT_BAD_ARG;
		}
	} ();

	closeOstream(&ctx);
	closeIstream(&ctx);

	if(LZ4MT_RESULT_OK != e) {
		opt.display("lz4mt: " + std::string(lz4mtResultToString(e)) + "\n");
		exit(EXIT_FAILURE);
	}

#if defined(_MSC_VER) && defined(_DEBUG)
	return EXIT_SUCCESS;
#else
	exit(EXIT_SUCCESS);
#endif
}
