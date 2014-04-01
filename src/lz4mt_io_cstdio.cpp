#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include "lz4mt_io_cstdio.h"
#include "lz4mt.h"

namespace {
std::string stdinFilename = "stdin";
std::string stdoutFilename = "stdout";
std::string nullFilename = "null";

bool isNullFp(const Lz4MtContext* ctx, FILE* fp) {
	return reinterpret_cast<const FILE*>(ctx) == fp;
}

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
	(void) _setmode(_fileno(stdin), _O_BINARY);
#endif
	return stdin;
}

FILE* getStdout() {
#ifdef _WIN32
	(void) _setmode(_fileno(stdout), _O_BINARY);
#endif
	return stdout;
}
} // anonymous namespace

namespace Lz4Mt { namespace Cstdio {
bool fileExist(const std::string& filename) {
	if(stdinFilename == filename || stdoutFilename == filename) {
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
	if(stdinFilename == filename) {
		fp = getStdin();
	} else {
		fp = fopen_(filename.c_str(), "rb");
	}
	ctx->readCtx = fp;
	return nullptr != fp;
}

bool openOstream(Lz4MtContext* ctx, const std::string& filename, bool nullWrite) {
	FILE* fp = nullptr;
	if(nullWrite) {
		fp = reinterpret_cast<FILE*>(ctx);
	} else if(stdoutFilename == filename) {
		fp = getStdout();
	} else {
		fp = fopen_(filename.c_str(), "wb");
	}
	ctx->writeCtx = fp;
	return nullptr != fp;
}

void closeIstream(Lz4MtContext* ctx) {
	fclose_(readCtx(ctx));
	ctx->readCtx = nullptr;
}

void closeOstream(Lz4MtContext* ctx) {
	auto* fp = writeCtx(ctx);
	if(!isNullFp(ctx, fp)) {
		fclose_(fp);
	}
	ctx->writeCtx = nullptr;
}

int read(Lz4MtContext* ctx, void* dst, int dstSize) {
	if(auto* fp = readCtx(ctx)) {
		return static_cast<int>(::fread(dst, 1, dstSize, fp));
	} else {
		return 0;
	}
}

int readSkippable(const Lz4MtContext* ctx
				  , uint32_t //magicNumber
				  , size_t size)
{
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
		if(isNullFp(ctx, fp)) {
			return sourceSize;
		}
		return static_cast<int>(::fwrite(source, 1, sourceSize, fp));
	} else {
		return 0;
	}
}

uint64_t getFilesize(const std::string& filename) {
	int r = 0;
#if defined(_MSC_VER)
	struct _stat64 s = { 0 };
	r = _stat64(filename.c_str(), &s);
	auto S_ISREG = [](decltype(s.st_mode) x) {
		return (x & S_IFMT) == S_IFREG;
	};
#else
	struct stat s;
	r = stat(filename.c_str(), &s);
#endif
	if(r || !S_ISREG(s.st_mode)) {
		return 0;
	} else {
		return static_cast<uint64_t>(s.st_size);
	}
}

std::string getStdinFilename() {
	return stdinFilename;
}

std::string getStdoutFilename() {
	return stdoutFilename;
}

std::string getNullFilename() {
	return nullFilename;
}

bool isAttyStdin() {
#if defined(_MSC_VER)
	return 0 != _isatty(_fileno(stdin));
#else
	return 0 != isatty(fileno(stdin));
#endif
}

bool isAttyStdout() {
#if defined(_MSC_VER)
	return 0 != _isatty(_fileno(stdout));
#else
	return 0 != isatty(fileno(stdout));
#endif
}

bool compareFilename(const std::string& lhs, const std::string& rhs) {
	const auto l = lhs.c_str();
	const auto r = rhs.c_str();
#if defined(_WIN32)
	return 0 == _stricmp(l, r);
#else
	return 0 == strcmp(l, r);
#endif
}

bool hasExtension(const std::string& filename, const std::string& extension) {
	const auto pos = filename.find_last_of('.');
	if(std::string::npos == pos) {
		return false;
	}
	const auto ext = filename.substr(pos);
	return compareFilename(ext, extension);
}

std::string removeExtension(const std::string& filename) {
	const auto o = filename.find_last_of('.');
	return filename.substr(0, o);
}

}} // namespace Cstdio, Lz4Mt
