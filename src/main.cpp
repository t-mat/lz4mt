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

// DISABLE_LZ4C_LEGACY_OPTIONS :
// Control the availability of -c0, -c1 and -hc legacy arguments
// Default : Legacy options are enabled
// #define DISABLE_LZ4C_LEGACY_OPTIONS

// DISABLE_LZ4MT_EXCLUSIVE_OPTIONS :
// #define DISABLE_LZ4MT_EXCLUSIVE_OPTIONS


namespace {

const char LZ4MT_EXTENSION[] = ".lz4";
const char LZ4MT_UNLZ4[] = "unlz4";
const char LZ4MT_LZ4CAT[] = "lz4cat";

const char welcomeMessage[] = 
	"*** lz4mt ***\n"
;

const char usage[] = 
	"usage :\n"
	"  ${lz4mt} [arg] [input] [output]\n"
	"\n"
	"input   : a filename\n"
	"          with no FILE, or when FILE is - or ${stdinmark}, read standard input\n"
	"Arguments :\n"
	" -1     : Fast compression (default)\n"
	" -9     : High compression\n"
	" -d     : decompression (default for ${.lz4} extension)\n"
	" -z     : force compression\n"
	" -f     : overwrite output without prompting\n"
	" -h/-H  : display help/long help and exit\n"
;

const char usage_advanced[] =
	"\n"
	"Advanced arguments :\n"
	" -V     : display Version number and exit\n"
	" -v     : verbose mode\n"
	" -q     : suppress warnings; specify twice to suppress errors too\n"
	" -c     : force write to standard output, even if it is the console\n"
	" -t     : test compressed file integrity\n"
	" -l     : compress using Legacy format (Linux kernel compression)\n"
	" -B#    : Block size [4-7](default : 7)\n"
	" -BD    : Block dependency (improve compression ratio)\n"
	" -BX    : enable block checksum (default:disabled)\n"
	" -Sx    : disable stream checksum (default:enabled)\n"
	"Benchmark arguments :\n"
	" -b     : benchmark file(s)\n"
	" -i#    : iteration loops [1-9](default : 3), benchmark mode only\n"
#if !defined(DISABLE_LZ4C_LEGACY_OPTIONS)
	"Legacy arguments :\n"
	" -c0    : fast compression\n"
	" -c1    : high compression\n"
	" -hc    : high compression\n"
	" -y     : overwrite output without prompting\n"
	" -s     : suppress warnings\n"
#endif // DISABLE_LZ4C_LEGACY_OPTIONS

#if !defined(DISABLE_LZ4MT_EXCLUSIVE_OPTIONS)
	"\n"
	"lz4mt exclusive arguments :\n"
	" --lz4mt-thread=0 : Multi thread mode (default)\n"
	" --lz4mt-thread=1 : Single thread mode\n"
#endif // DISABLE_LZ4MT_EXCLUSIVE_OPTIONS
;

const char usage_longHelp[] =
	"\n"
	"Which values can get [output] ?\n"
	"[output] : a filename\n"
	"          '${stdout}', or '-' for standard output (pipe mode)\n"
	"          '${null}' to discard output (test mode)\n"
	"[output] can be left empty. In this case, it receives the following value :\n"
	"          - if stdout is not the console, then [output] = stdout\n"
	"          - if stdout is console :\n"
	"               + if compression selected, output to filename${.lz4}\n"
	"               + if decompression selected, output to filename without '${.lz4}'\n"
	"                    > if input filename has no '${.lz4}' extension : error\n"
	"\n"
	"Compression levels :\n"
	"There are technically 2 accessible compression levels.\n"
	"-0 ... -2 => Fast compression\n"
	"-3 ... -9 => High compression\n"
	"\n"
	"stdin, stdout and the console :\n"
	"To protect the console from binary flooding (bad argument mistake)\n"
	"${lz4mt} will refuse to read from console, or write to console\n"
	"except if '-c' command is specified, to force output to console\n"
	"\n"
	"Simple example :\n"
	"1 : compress 'filename' fast, using default output name 'filename.lz4'\n"
	"          ${lz4mt} filename\n"
	"\n"
	"Arguments can be appended together, or provided independently. For example :\n"
	"2 : compress 'filename' in high compression mode, overwrite output if exists\n"
	"          ${lz4mt} -f9 filename\n"
	"    is equivalent to :\n"
	"          ${lz4mt} -f -9 filename\n"
	"\n"
	"${lz4mt} can be used in 'pure pipe mode', for example :\n"
	"3 : compress data stream from 'generator', send result to 'consumer'\n"
	"          generator | ${lz4mt} | consumer\n"
#if !defined(DISABLE_LZ4C_LEGACY_OPTIONS)
	"\n"
	"Warning :\n"
	"Legacy arguments take precedence. Therefore :\n"
	"          ${lz4mt} -hc filename\n"
	"means 'compress filename in high compression mode'\n"
	"It is not equivalent to :\n"
	"          ${lz4mt} -h -c filename\n"
	"which would display help text and exit\n"
#endif // DISABLE_LZ4C_LEGACY_OPTIONS
;

typedef std::function<bool(void)> AttyFunc;
typedef std::function<bool(const std::string&, const std::string&)> CmpFunc;


enum class DisplayLevel {
	  NO_DISPLAY
	, ERRORS
	, RESULTS
	, PROGRESSION
	, INFORMATION
	, MIN = NO_DISPLAY
	, MAX = INFORMATION
	, DEFAULT = RESULTS
};

DisplayLevel& operator--(DisplayLevel& x) {
	if(DisplayLevel::NO_DISPLAY != x) {
		x = static_cast<DisplayLevel>(static_cast<int>(x) - 1);
	}
	return x;
}


bool hasExtension(const std::string& str, const std::string& ext) {
	const auto pos = str.find_last_of('.');
	if(std::string::npos == pos) {
		return false;
	}
	return 0 == str.compare(pos, ext.length(), ext);
}


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
		, pause(false)
		, compMode(CompMode::COMPRESS)
		, sd(lz4mtInitStreamDescriptor())
		, mode(LZ4MT_MODE_DEFAULT)
		, inpFilename()
		, outFilename()
		, nullWrite(false)
		, overwrite(false)
		, silence(false)
		, benchmark()
		, forceCompress(false)
		, forceStdout(false)
		, compressionLevel(0)
		, displayLevel(DisplayLevel::DEFAULT)
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

		auto isDigits = [](const std::string& s) {
			return std::all_of(std::begin(s), std::end(s)
							   , static_cast<int(*)(int)>(std::isdigit));
		};

		replaceMap = [&]() {
			return std::map<std::string, std::string> {
				  { "${lz4mt}"		, argv[0] }
				, { "${.lz4}"		, LZ4MT_EXTENSION }
				, { "${stdinmark}"	, stdinFilename }
				, { "${stdout}"		, stdoutFilename }
				, { "${null}"		, nullFilename }
			};
		};

		std::map<std::string, std::function<bool (const std::string&)>> opts;

		const char optdelim = '=';

		auto getOptionName = [&](const std::string& s) -> std::string {
			const auto pos = s.find(optdelim);
			if(std::string::npos != pos) {
				return s.substr(0, pos);
			} else {
				return s;
			}
		};

		auto getOptionArg = [&](const std::string& s) -> std::string {
			const auto pos = s.find(optdelim);
			if(std::string::npos != pos) {
				return s.substr(pos+1);
			} else {
				return "";
			}
		};

		auto findOption = [&](const std::string& s) {
			return opts.find(getOptionName(s));
		};

#if !defined(DISABLE_LZ4MT_EXCLUSIVE_OPTIONS)
		opts["--lz4mt-thread"] = [&](const std::string& arg) -> bool {
			auto a = getOptionArg(arg);
			if(isDigits(a)) {
				const auto v = std::stoi(a);
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
#endif // DISABLE_LZ4MT_EXCLUSIVE_OPTIONS

		while(!args.empty() && !error && !exitFlag) {
			const auto a = args.front();
			args.pop_front();
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
				const auto it = findOption(a);
				if(opts.end() == it) {
					errorString += "lz4mt: Bad argument ["
								   + std::string(a) + "]\n";
					error = true;
				} else {
					const auto b = it->second(a);
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

#if !defined(DISABLE_LZ4C_LEGACY_OPTIONS)
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

					if(getif2('c', '0')) {					// -c0
						compMode = CompMode::COMPRESS;
						compressionLevel = 1;
					} else if(getif2('c', '1')) {			// -c1
						compMode = CompMode::COMPRESS;
						compressionLevel = 9;
					} else if(getif2('h', 'c')) {			// -hc
						compMode = CompMode::COMPRESS;
						compressionLevel = 9;
					} else if(getif('y')) {					// -y
						overwrite = true;
					} else if(getif('s')) {					// -s
						displayLevel = DisplayLevel::ERRORS;
					} else
#endif // DISABLE_LZ4C_LEGACY_OPTIONS

					if(getif('V')) {						// -V
						showWelcomeMessage();
						exitFlag = true;
					} else if(getif('h')) {					// -h
						showUsage(true);
						exitFlag = true;
					} else if(getif('H')) {					// -H
						showUsage(true, true);
						exitFlag = true;
					} else if(getif('z')) {					// -z
						forceCompress = true;
					} else if(getif('1')) {					// -1
						compMode = CompMode::COMPRESS;
						compressionLevel = 1;
					} else if(getif('2')) {					// -2
						compMode = CompMode::COMPRESS;
						compressionLevel = 2;
					} else if(getif('3')) {					// -3
						compMode = CompMode::COMPRESS;
						compressionLevel = 3;
					} else if(getif('4')) {					// -4
						compMode = CompMode::COMPRESS;
						compressionLevel = 4;
					} else if(getif('5')) {					// -5
						compMode = CompMode::COMPRESS;
						compressionLevel = 5;
					} else if(getif('6')) {					// -6
						compMode = CompMode::COMPRESS;
						compressionLevel = 6;
					} else if(getif('7')) {					// -7
						compMode = CompMode::COMPRESS;
						compressionLevel = 7;
					} else if(getif('8')) {					// -8
						compMode = CompMode::COMPRESS;
						compressionLevel = 8;
					} else if(getif('9')) {					// -9
						compMode = CompMode::COMPRESS;
						compressionLevel = 9;
					} else if(getif('A')) {					// -A
						// non documented (hidden)
						compMode = CompMode::COMPRESS;
						compressionLevel = 'A' - '0';
//					} else if(getif('l')) {					// -l
//						legacyFormat = true;
					} else if(getif('d')) {					// -d
						compMode = CompMode::DECOMPRESS;
					} else if(getif('c')) {					// -c
						// TODO : Implement
						forceStdout = true;
						outFilename = stdoutFilename;
						displayLevel = DisplayLevel::ERRORS;
					} else if(getif('t')) {					// -t
						compMode = CompMode::DECOMPRESS;
						outFilename = nullFilename;
					} else if(getif('f')) {					// -f
						overwrite = true;
					} else if(getif('v')) {					// -v
						displayLevel = DisplayLevel::MAX;
					} else if(getif('q')) {					// -q
						--displayLevel;
					} else if(getif('k')) {					// -k
						// keep source file (default anyway, so useless)
						// (for xz/lzma compatibility)
					} else if(getif('B')) {					// -B?
						for(;;) {
							if(getif('4')) {				// -B4
								sd.bd.blockMaximumSize = 4;
							} else if(getif('5')) {			// -B5
								sd.bd.blockMaximumSize = 5;
							} else if(getif('6')) {			// -B6
								sd.bd.blockMaximumSize = 6;
							} else if(getif('7')) {			// -B7
								sd.bd.blockMaximumSize = 7;
							} else if(getif('D')) {			// -BD
								// TODO : Implement
								sd.flg.blockIndependence = 0;
							} else if(getif('X')) {			// -BX
								sd.flg.blockChecksum = 1;
							} else {						// -B?
								// NOTE: no bad usage
								break;
							}
						}
					} else if(getif('S')) {					// -S?
						if(getif('x')) {					// -Sx
							sd.flg.streamChecksum = 0;
						} else {
							showBadUsage(a[i], a[i+1]);
							error = true;
						}
					} else if(getif('b')) {					// -b
						compMode = CompMode::COMPRESS;
						benchmark.enable = true;
					} else if(getif('i')) {
						for(char x = '1'; x <= '9'; ++x) {	// -i[1-9]
							if(getif(x)) {
								benchmark.nIter = x - '0';
								benchmark.enable = true;
								break;
							}
						}
						// NOTE: no bad usage
					} else if(getif('p')) {					// -p
						// Pause at the end (hidden option)
						// TODO : Implement
						benchmark.pause = true;
						pause = true;
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

			if(isCompress() && !forceCompress) {
				if(hasExtension(inpFilename, LZ4MT_EXTENSION)) {
					compMode = CompMode::DECOMPRESS;
				}
			}

			if(outFilename.empty()) {
				if(cmpFilename(stdinFilename, inpFilename)) {
					outFilename = stdoutFilename;
					silence = true;
				} else if(isCompress()) {
					outFilename = inpFilename + LZ4MT_EXTENSION;
				} else {
					if(hasExtension(inpFilename, LZ4MT_EXTENSION)) {
						const auto o = inpFilename.find_last_of('.');
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
		return CompMode::COMPRESS == compMode;
	}

	bool isDecompress() const {
		return CompMode::DECOMPRESS == compMode;
	}

	void showWelcomeMessage() {
		errorString += replace(welcomeMessage);
	}

	void showUsage(bool advanced = false, bool longHelp = false) {
		errorString += replace(usage);
		if(advanced) {
			errorString += replace(usage_advanced);
		}
		if(longHelp) {
			errorString += replace(usage_longHelp);
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

	void display(DisplayLevel displayLevel, const std::string& message) {
		if(this->displayLevel >= displayLevel) {
			display(message);
		}
	}

	std::string replace(const std::string& s0) {
		auto s = s0;
		const std::map<std::string, std::string> rm = replaceMap();
		for(const auto& r : rm) {
			const auto& from = r.first;
			const auto& to = r.second;
			for(;;) {
				const auto pos = s.find(from);
				if(std::string::npos == pos) {
					break;
				}
				s.replace(pos, from.length(), to);
			}
		}
		return s;
	}

	enum class CompMode {
		  DECOMPRESS
		, COMPRESS
	};

	bool error;
	bool exitFlag;
	bool pause;
	CompMode compMode;
	Lz4MtStreamDescriptor sd;
	int mode;
	std::string inpFilename;
	std::string outFilename;
	bool nullWrite;
	bool overwrite;
	bool silence;
	Lz4Mt::Benchmark benchmark;
	bool forceCompress;
	bool forceStdout;
	int compressionLevel;
	DisplayLevel displayLevel;
	std::string errorString;
	std::function<std::map<std::string, std::string> ()> replaceMap;
};


int lz4mtCommandLine(int argc, char* argv[]) {
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
		return EXIT_SUCCESS;
	} else if(opt.error) {
		opt.display(opt.errorString);
		return EXIT_FAILURE;
	}

	Lz4MtContext ctx = lz4mtInitContext();
	ctx.mode				= static_cast<Lz4MtMode>(opt.mode);
	ctx.read				= read;
	ctx.readSeek			= readSeek;
	ctx.readEof				= readEof;
	ctx.write				= write;
	ctx.compress			= [&opt]() {
		if(opt.compressionLevel >= 3) {
			return LZ4_compressHC2_limitedOutput;
		} else {
			return [](const char* src, char* dst, int size, int maxOut, int) {
				return LZ4_compress_limitedOutput(src, dst, size, maxOut);
			};
		}
	}();
	ctx.compressBound		= LZ4_compressBound;
	ctx.decompress			= LZ4_decompress_safe;
	ctx.compressionLevel	= opt.compressionLevel;

	if(opt.benchmark.enable) {
		opt.benchmark.openIstream	= openIstream;
		opt.benchmark.closeIstream	= closeIstream;
		opt.benchmark.getFilesize	= getFilesize;
		opt.benchmark.measure(ctx, opt.sd);
		return EXIT_SUCCESS;
	}

	if(!openIstream(&ctx, opt.inpFilename)) {
		opt.display("lz4mt: Can't open input file ["
					+ opt.inpFilename + "]\n");
		return EXIT_FAILURE;
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
			return EXIT_FAILURE;
		}
	}

	if(!openOstream(&ctx, opt.outFilename, opt.nullWrite)) {
		opt.display("lz4mt: Can't open output file ["
					+ opt.outFilename + "]\n");
		return EXIT_FAILURE;
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
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

} // anonymous namespace


int main(int argc, char* argv[]) {
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	const auto exitCode = lz4mtCommandLine(argc, argv);

#if defined(_MSC_VER) && defined(_DEBUG)
	return exitCode;
#else
	exit(exitCode);
#endif
}
