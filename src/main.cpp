#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include <algorithm>
#include <deque>
#include <cctype>
#include <exception>
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

// DISABLE_LZ4MT_EXCLUSIVE_ERROR :
#define DISABLE_LZ4MT_EXCLUSIVE_ERROR


namespace {

const char LZ4MT_EXTENSION[] = ".lz4";

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
typedef std::function<bool(const std::string&, const std::string&)> HasExtensionFunc;
typedef std::function<std::string(const std::string&)> RemoveExtensionFunc;
typedef std::map<std::string, std::string> ReplaceMap;


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


namespace Exception {
struct BadUsage : std::exception {
	const char* what() const throw() {
	//	return "Incorrect parameters";
		return "";
	}
};


struct ExitError : std::exception {
	ExitError(int errorCode)
		: errorCode(errorCode)
	{}

	const char* what() const throw() {
		return "";
	}

	int errorCode;
};


struct Lz4MtError : std::exception {
	Lz4MtError(Lz4MtResult lz4MtResult)
		: lz4MtResult(lz4MtResult)
	{}

	const char* what() const throw() {
		return "";
	}

	Lz4MtResult lz4MtResult;
};


struct ExitGracefully : std::exception {
	const char* what() const throw() {
		return "";
	}
};
} // namespace Exception

enum class CompMode {
	  DECOMPRESS
	, COMPRESS
};


class CompressionMode {
public:
	CompressionMode(CompMode compMode, int compressionLevel)
		: compMode(compMode)
		, compressionLevel(compressionLevel)
	{}

	bool isCompress() const {
		return CompMode::COMPRESS == compMode;
	}

	bool isDecompress() const {
		return CompMode::DECOMPRESS == compMode;
	}

	int getCompressionLevel() const {
		return compressionLevel;
	}

	void set(CompMode compMode, int compressionLevel = -1) {
		this->compMode = compMode;
		if(compressionLevel >= 0) {
			this->compressionLevel = compressionLevel;
		}
	}

private:
	CompMode compMode;
	int compressionLevel;
};


class Output {
public:
	typedef std::function<void(const std::string&)> OutputFunction;

	Output(OutputFunction outputFunction)
		: outputFunction(outputFunction)
		, displayLevel(DisplayLevel::DEFAULT)
		, silence(false)
	{}

	void display(const std::string& message) const {
		if(!silence) {
			outputFunction(message);
		//	std::cerr << message;
		}
	}

	void display(const std::exception& e) const {
		display(e.what());
	}

	void setDisplayLevel(DisplayLevel displayLevel) {
		this->displayLevel = displayLevel;
	}

	DisplayLevel getDisplayLevel() const {
		return displayLevel;
	}

	void decreaseDisplayLevel() {
		--displayLevel;
	}

	bool checkDisplayLevel(DisplayLevel displayLevel) const {
		return this->displayLevel >= displayLevel;
	}

	void display(DisplayLevel displayLevel, const std::string& message) const {
		if(checkDisplayLevel(displayLevel)) {
			display(message);
		}
	}

protected:
	OutputFunction outputFunction;
	DisplayLevel displayLevel;
	bool silence;
};


struct Option {
	Option(
		  Output& output
		, int argc
		, char* argv[]
		, std::string stdinFilename
		, std::string stdoutFilename
		, std::string nullFilename
		, AttyFunc isAttyStdout
		, AttyFunc isAttyStdin
		, CmpFunc cmpFilename
		, HasExtensionFunc hasExtension
		, RemoveExtensionFunc removeExtension
	)
		: output(output)
		, pause(false)
		, compressionMode(CompMode::COMPRESS, 0)
		, sd(lz4mtInitStreamDescriptor())
		, mode(LZ4MT_MODE_DEFAULT)
		, inpFilename()
		, outFilename()
		, nullWrite(false)
		, overwrite(false)
		, benchmark()
		, forceCompress(false)
		, forceStdout(false)
		, replaceMap()
	{
		std::deque<std::string> args;
		for(int iarg = 1; iarg < argc; ++iarg) {
			args.push_back(argv[iarg]);
		}

		replaceMap = [&]() -> ReplaceMap {
			// NOTE for "-> ReplaceMap" :
			//		It's a workaround for g++-4.6's strange warning.

			ReplaceMap rm;
			rm["${lz4mt}"]		= argv[0];
			rm["${.lz4}"]		= LZ4MT_EXTENSION;
			rm["${stdinmark}"]	= stdinFilename;
			rm["${stdout}"]		= stdoutFilename;
			rm["${null}"]		= nullFilename;
			return rm;
		};

		const auto isStdinFilename = [&](const std::string& str) {
			return cmpFilename(stdinFilename, str);
		};

		const auto isStdoutFilename = [&](const std::string& str) {
			return cmpFilename(stdoutFilename, str);
		};

		const auto isNullFilename = [&](const std::string& str) {
			return cmpFilename(nullFilename, str);
		};

		std::map<int, std::function<void()>> options;
		options['V'] = [&]() {
			output.display(replace(welcomeMessage));
			throw Exception::ExitGracefully();
		};
		options['h'] = [&]() {
			showUsage(true);
			throw Exception::ExitGracefully();
		};
		options['H'] = [&]() {
			showUsage(true, true);
			throw Exception::ExitGracefully();
		};
		options['z'] = [&]() { forceCompress = true; };
		options['1'] = [&]() { compressionMode.set(CompMode::COMPRESS, 1); };
		options['2'] = [&]() { compressionMode.set(CompMode::COMPRESS, 2); };
		options['3'] = [&]() { compressionMode.set(CompMode::COMPRESS, 3); };
		options['4'] = [&]() { compressionMode.set(CompMode::COMPRESS, 4); };
		options['5'] = [&]() { compressionMode.set(CompMode::COMPRESS, 5); };
		options['6'] = [&]() { compressionMode.set(CompMode::COMPRESS, 6); };
		options['7'] = [&]() { compressionMode.set(CompMode::COMPRESS, 7); };
		options['8'] = [&]() { compressionMode.set(CompMode::COMPRESS, 8); };
		options['9'] = [&]() { compressionMode.set(CompMode::COMPRESS, 9); };
		options['A'] = [&]() { compressionMode.set(CompMode::COMPRESS, 'A' - '0'); };
	//	options['l'] = [&]() { legacyFormat = true; };
		options['d'] = [&]() { compressionMode.set(CompMode::DECOMPRESS); };
		options['c'] = [&]() {
			forceStdout = true;
			outFilename = stdoutFilename;
			output.setDisplayLevel(DisplayLevel::ERRORS);
		};
		options['t'] = [&]() {
			compressionMode.set(CompMode::DECOMPRESS);
			outFilename = nullFilename;
		};
		options['f'] = [&]() { overwrite = true; };
		options['v'] = [&]() { output.setDisplayLevel(DisplayLevel::MAX); };
		options['q'] = [&]() { output.decreaseDisplayLevel(); };
		options['k'] = [&]() {
			// keep source file (default anyway, so useless)
			// (for xz/lzma compatibility)
		};
		options['b'] = [&]() {
			compressionMode.set(CompMode::COMPRESS);
			benchmark.enable = true;
		};
		options['p'] = [&]() {
			// Pause at the end (hidden option)
			// TODO : Implement
			benchmark.pause = true;
			pause = true;
		};

#if !defined(DISABLE_LZ4C_LEGACY_OPTIONS)
		options['y'] = [&]() { overwrite = true; };
		options['s'] = [&]() { output.setDisplayLevel(DisplayLevel::ERRORS); };
		options[('c' << 8) + '0'] = [&]() { compressionMode.set(CompMode::COMPRESS, 1); };
		options[('c' << 8) + '1'] = [&]() { compressionMode.set(CompMode::COMPRESS, 9); };
		options[('h' << 8) + 'c'] = [&]() { compressionMode.set(CompMode::COMPRESS, 9); };
#endif

#if !defined(DISABLE_LZ4MT_EXCLUSIVE_OPTIONS)
		auto isDigits = [](const std::string& s) {
			return std::all_of(std::begin(s), std::end(s)
							   , static_cast<int(*)(int)>(std::isdigit));
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
				output.display("lz4mt: Bad argument for --lz4mt-thread ["
					 + std::string(a) + "]\n");
				return false;
			}
		};
#endif // DISABLE_LZ4MT_EXCLUSIVE_OPTIONS

		while(!args.empty()) {
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
					// first provided filename is input
					inpFilename = a;
				} else if(outFilename.empty()) {
					// second provided filename is output
					outFilename = a;
				} else {
#if !defined(DISABLE_LZ4MT_EXCLUSIVE_ERROR)
					show("lz4mt: Bad argument [" + std::string(a) + "]\n");
					throw Exception::BadUsage();
#endif
				}
			} else if('-' == a0 && 0 == a1) {
				if(inpFilename.empty()) {
					inpFilename = stdinFilename;
				} else {
					outFilename = stdoutFilename;
				}
			}
#if !defined(DISABLE_LZ4MT_EXCLUSIVE_OPTIONS)
			else if('-' == a0 && '-' == a1) {
				//	long option
				const auto it = findOption(a);
				if(opts.end() == it) {
					output.display("lz4mt: Bad argument [" + std::string(a) + "]\n");
					throw Exception::BadUsage();
				} else {
					const auto b = it->second(a);
					if(!b) {
						throw Exception::BadUsage();
					}
				}
			}
#endif
			else {
				for(int i = 1; 0 != a[i];) {
					bool processed = false;

					if(!processed && a[i]) {
						const auto it = options.find((a[i] << 8) + a[i+1]);
						if(options.end() != it) {
							it->second();
							processed = true;
							i += 2;
						}
					}

					if(!processed && a[i]) {
						const auto it = options.find(a[i]);
						if(options.end() != it) {
							it->second();
							processed = true;
							i += 1;
						}
					}

					const auto getif = [&] (char c0) -> bool {
						const auto x0 = a[i];
						if(x0 == c0) {
							++i;
							return true;
						} else {
							return false;
						}
					};

					if(!processed && getif('B')) {
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
						processed = true;
					}

					if(!processed && getif('S')) {
						if(getif('x')) {					// -Sx
							sd.flg.streamChecksum = 0;
						} else {
							showBadUsage(a[i], a[i+1]);
							throw Exception::BadUsage();
						}
						processed = true;
					}

					if(!processed && getif('i')) {
						for(char x = '1'; x <= '9'; ++x) {	// -i[1-9]
							if(getif(x)) {
								benchmark.nIter = x - '0';
								benchmark.enable = true;
								break;
							}
						}
						// NOTE: no bad usage
						processed = true;
					}

					if(!processed) {
						// Unrecognised command
						showBadUsage(a[i]);
						throw Exception::BadUsage();
					}
				}
			}
		}

		output.display(DisplayLevel::PROGRESSION, welcomeMessage);

		//
		// TODO : Investigate about 'blockSize'.
		//	in lz4cli.c, blockSize is always 4096KiB.
		//
		//	 - Is this a expected behaviour ?
		//	 - It seems that return value of LZ4IO_setBlockSizeID() in '-B[4-7]' should substitute to blockSize.
		//
		//	display(DisplayLevel::INFORMATION, "Blocks size : " + std::to_string(blockSize >> 10) + "KB\n");

		// No input filename ==> use stdin
		if(inpFilename.empty()) {
			inpFilename = stdinFilename;
		}

		// Check if input or output are defined as console;
		// trigger an error in this case
		if(isStdinFilename(inpFilename) && isAttyStdin()) {
			showBadUsage();
			throw Exception::BadUsage();
		}

		// No output filename ==> try to select one automatically (when possible)
		if(outFilename.empty()) {
			// Default to stdout whenever possible (i.e. not a console)
			if(!isAttyStdout()) {
				outFilename = stdinFilename;
			} else {
				// auto-determine compression or decompression, based on file extension
				if(compressionMode.isCompress() && !forceCompress) {
					if(hasExtension(inpFilename, LZ4MT_EXTENSION)) {
						compressionMode.set(CompMode::DECOMPRESS);
					}
				}

				if(compressionMode.isCompress()) {
					// compression to file
					outFilename = inpFilename + LZ4MT_EXTENSION;
					output.display(DisplayLevel::RESULTS, "Compressed filename will be : " + outFilename + "\n");
				} else {
					// decompression to file (automatic name will work
					// only if input filename has correct format extension)
					if(hasExtension(inpFilename, LZ4MT_EXTENSION)) {
						outFilename = removeExtension(inpFilename);
						output.display(DisplayLevel::RESULTS, "Decoding file " + outFilename + "\n");
					} else {
						output.display(DisplayLevel::ERRORS, "Cannot determine an output filename\n");
						throw Exception::BadUsage();
					}
				}
			}
		}

		if(isNullFilename(outFilename)) {
			nullWrite = true;
		}


		// No warning message in pure pipe mode (stdin + stdout)
		if(   isStdinFilename(inpFilename)
		   && isStdoutFilename(outFilename)
		   && DisplayLevel::PROGRESSION == output.getDisplayLevel()
		) {
			output.setDisplayLevel(DisplayLevel::ERRORS);
		}

		// Check if input or output are defined as console;
		// trigger an error in this case
		if(   (   isStdoutFilename(outFilename)
			   && isAttyStdout()
			   && !forceStdout
			  )
		   || (   isStdinFilename(inpFilename)
			   && isAttyStdin()
			  )
		) {
			showBadUsage();
			throw Exception::BadUsage();
		}
	}

	void showUsage(bool advanced = false, bool longHelp = false) {
		output.display(replace(usage));
		if(advanced) {
			output.display(replace(usage_advanced));
		}
		if(longHelp) {
			output.display(replace(usage_longHelp));
		}
	}

	void showBadUsage(char c0 = 0, char c1 = 0) {
		output.display(DisplayLevel::ERRORS, "Incorrect parameters\n");
		if(c0 || c1) {
#if !defined(DISABLE_LZ4MT_EXCLUSIVE_ERROR)
			output.display("Wrong parameters '");
			if(c0) {
				output.display(c0);
			}
			if(c1) {
				output.display(c1);
			}
			output.display("'\n");
#endif
		}
		if(output.checkDisplayLevel(DisplayLevel::ERRORS)) {
			showUsage(false);
		}
	}

	std::string replace(const std::string& s0) const {
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

	Option& operator=(const Option&);

	Output& output;
	bool pause;
	CompressionMode compressionMode;
	Lz4MtStreamDescriptor sd;
	int mode;
	std::string inpFilename;
	std::string outFilename;
	bool nullWrite;
	bool overwrite;
	Lz4Mt::Benchmark benchmark;
	bool forceCompress;
	bool forceStdout;
	std::function<ReplaceMap()> replaceMap;
};


typedef int (*CompressionFunc)
	(const char* src, char* dst, int size, int maxOut, int maxOutputSize);

int bridge_LZ4_compress_limitedOutput(const char* src, char* dst, int size, int maxOut, int) {
	return LZ4_compress_limitedOutput(src, dst, size, maxOut);
}


int lz4mtCommandLine(Output& output, int argc, char* argv[]) {
	using namespace Lz4Mt::Cstdio;
	Option opt(output, argc, argv
			   , getStdinFilename()
			   , getStdoutFilename()
			   , getNullFilename()
			   , isAttyStdout
			   , isAttyStdin
			   , compareFilename
			   , hasExtension
			   , removeExtension
	);

	Lz4MtContext ctx = lz4mtInitContext();
	ctx.mode				= static_cast<Lz4MtMode>(opt.mode);
	ctx.read				= read;
	ctx.readSeek			= readSeek;
	ctx.readEof				= readEof;
	ctx.write				= write;
	ctx.compressBound		= LZ4_compressBound;
	ctx.decompress			= LZ4_decompress_safe;
	ctx.compressionLevel	= opt.compressionMode.getCompressionLevel();
	ctx.compress			= [&ctx]() -> CompressionFunc {
		// NOTE for "-> CompressionFunc" :
		//		It's a workaround for g++-4.6's strange warning.

		if(ctx.compressionLevel >= 3) {
			return LZ4_compressHC2_limitedOutput;
		} else {
			return bridge_LZ4_compress_limitedOutput;
		}
	}();

	// Check if benchmark is selected
	if(opt.benchmark.enable) {
		opt.benchmark.openIstream	= openIstream;
		opt.benchmark.closeIstream	= closeIstream;
		opt.benchmark.getFilesize	= getFilesize;
		opt.benchmark.measure(ctx, opt.sd);
		return EXIT_SUCCESS;
	}

	if(!openIstream(&ctx, opt.inpFilename)) {
		output.display(DisplayLevel::ERRORS
					   , "Pb opening " + opt.inpFilename + "\n");
		throw Exception::ExitError(12);
	}

	// Check if destination file already exists
	if(!opt.nullWrite && !opt.overwrite && fileExist(opt.outFilename)) {
		output.display(DisplayLevel::RESULTS
					   , "Warning : " + opt.outFilename + " already exists\n");
		output.display(DisplayLevel::RESULTS
					   , "Overwrite ? (Y/N) : ");

		if(output.checkDisplayLevel(DisplayLevel::RESULTS)) {
			output.display(DisplayLevel::ERRORS
						   , "Option aborted : " + opt.outFilename + " already exists\n");
			throw Exception::ExitError(11);
		}

		const int ch = [&]() -> int {
			if(!output.checkDisplayLevel(DisplayLevel::RESULTS)) {
				return std::cin.get();
			} else {
				return 0;
			}
		} ();

		if('y' != ch && 'Y' != ch) {
			output.display(DisplayLevel::ERRORS
						   , "Option aborted : " + opt.outFilename + " already exists\n");
			throw Exception::ExitError(11);
		}
	}

	if(!openOstream(&ctx, opt.outFilename, opt.nullWrite)) {
		output.display(DisplayLevel::ERRORS
					   , "Pb opening " + opt.outFilename + "\n");
		throw Exception::ExitError(13);
	}

	const auto e = [&]() -> Lz4MtResult {
		if(opt.compressionMode.isCompress()) {
			return lz4mtCompress(&ctx, &opt.sd);
		} else if(opt.compressionMode.isDecompress()) {
			return lz4mtDecompress(&ctx, &opt.sd);
		} else {
			return LZ4MT_RESULT_ERROR;
		}
	} ();

	closeOstream(&ctx);
	closeIstream(&ctx);

	if(LZ4MT_RESULT_OK != e) {
		output.display("lz4mt: " + std::string(lz4mtResultToString(e)) + "\n");
		throw Exception::ExitError(static_cast<int>(e));
	}

	if(opt.pause) {
		output.display("Press enter to continue...\n");
		std::cin.get();
	}

	return EXIT_SUCCESS;
}


int lz4mtCommandLineDriver(int argc, char* argv[]) {
	const auto outputFunction = [](const std::string& message) {
		std::cerr << message;
	};

	Output output(outputFunction);

	int exitCode = EXIT_FAILURE;

	try {
		exitCode = lz4mtCommandLine(output, argc, argv);
	} catch(Exception::ExitGracefully&) {
		exitCode = EXIT_SUCCESS;
	} catch(Exception::Lz4MtError& e) {
		exitCode = lz4mtResultToLz4cExitCode(e.lz4MtResult);
	} catch(std::exception& e) {
		output.display(e);
	} catch(...) {
	}

	return exitCode;
}


} // anonymous namespace


int main(int argc, char* argv[]) {
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	const auto exitCode = lz4mtCommandLineDriver(argc, argv);

#if defined(_MSC_VER) && defined(_DEBUG)
	return exitCode;
#else
	exit(exitCode);
#endif
}
