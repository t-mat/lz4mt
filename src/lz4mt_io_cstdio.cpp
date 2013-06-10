#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "lz4mt_io_cstdio.h"
#include "lz4mt.h"

namespace {
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
}

namespace Lz4Mt { namespace Cstdio {
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
	ctx->readCtx = fp;
	return nullptr != fp;
}

bool openOstream(Lz4MtContext* ctx, const std::string& filename) {
	FILE* fp = nullptr;
	if("stdout" == filename) {
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
	fclose_(writeCtx(ctx));
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
	struct stat s;
	r = stat(fileanme.c_str(), &s);
#endif
	if(r || !S_ISREG(s.st_mode)) {
		return 0;
	} else {
		return static_cast<uint64_t>(s.st_size);
	}
}

}} // namespace Cstdio, Lz4Mt
