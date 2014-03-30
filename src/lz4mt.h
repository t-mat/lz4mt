#ifndef LZ4MT_H
#define LZ4MT_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif


struct Lz4MtParam;

typedef int (*Lz4MtRead)(
	  struct Lz4MtContext* ctx
	, void* dst
	, int dstSize
);

typedef int (*Lz4MtReadSeek)(
	  const struct Lz4MtContext* ctx
	, int offset
);

typedef int (*Lz4MtReadEof)(
	  const struct Lz4MtContext* ctx
);

typedef int (*Lz4MtReadSkippable)(
	  const struct Lz4MtContext* ctx
	, uint32_t magicNumber
	, size_t size
);

typedef int (*Lz4MtWrite)(
	  const struct Lz4MtContext* ctx
	, const void* src
	, int srcSize
);

typedef int (*Lz4MtCompress)(
	  const char* src
	, char* dst
	, int isize
	, int maxOutputSize
	, int compressionLevel
);

typedef int (*Lz4MtCompressBound)(
	  int isize
);

typedef int (*Lz4MtDecompress)(
	  const char* src
	, char* dst
	, int isize
	, int maxOutputSize
);


enum Lz4MtMode {
	  LZ4MT_MODE_DEFAULT		= 0
	, LZ4MT_MODE_PARALLEL		= 0 << 0
	, LZ4MT_MODE_SEQUENTIAL		= 1 << 0
};
typedef enum Lz4MtMode Lz4MtMode;


enum Lz4MtResult {
	  LZ4MT_RESULT_OK = 0
	, LZ4MT_RESULT_ERROR
	, LZ4MT_RESULT_INVALID_MAGIC_NUMBER
	, LZ4MT_RESULT_INVALID_HEADER
	, LZ4MT_RESULT_PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET
	, LZ4MT_RESULT_BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET
	, LZ4MT_RESULT_INVALID_VERSION
	, LZ4MT_RESULT_INVALID_HEADER_CHECKSUM
	, LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE
	, LZ4MT_RESULT_CANNOT_WRITE_HEADER
	, LZ4MT_RESULT_CANNOT_WRITE_EOS
	, LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM
	, LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE
	, LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA
	, LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM
	, LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM
	, LZ4MT_RESULT_BLOCK_CHECKSUM_MISMATCH
	, LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH
	, LZ4MT_RESULT_DECOMPRESS_FAIL
	, LZ4MT_RESULT_BAD_ARG
	, LZ4MT_RESULT_INVALID_BLOCK_SIZE
	, LZ4MT_RESULT_INVALID_HEADER_RESERVED1
	, LZ4MT_RESULT_INVALID_HEADER_RESERVED2
	, LZ4MT_RESULT_INVALID_HEADER_RESERVED3
	, LZ4MT_RESULT_INVALID_HEADER_SKIPPABLE_SIZE_UNREADABLE
	, LZ4MT_RESULT_INVALID_HEADER_CANNOT_SKIP_SKIPPABLE_AREA
	, LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK
	, LZ4MT_RESULT_CANNOT_WRITE_DECODED_BLOCK
};
typedef enum Lz4MtResult Lz4MtResult;


struct Lz4MtFlg {
	char	presetDictionary;	// bit[0]
	char	reserved1;			// bit[1]
	char	streamChecksum;		// bit[2]
	char	streamSize;			// bit[3]
	char	blockChecksum;		// bit[4]
	char	blockIndependence;	// bit[5]
	char	versionNumber;		// bit[6,7]
};
typedef struct Lz4MtFlg Lz4MtFlg;


struct Lz4MtBd {
	char	reserved3;			// bit[0,3]
	char	blockMaximumSize;	// bit[4,6]
	char	reserved2;			// bit[7]
};
typedef struct Lz4MtBd Lz4MtBd;


struct Lz4MtStreamDescriptor {
	Lz4MtFlg	flg;
	Lz4MtBd		bd;
	uint64_t	streamSize;
	uint32_t	dictId;
};
typedef struct Lz4MtStreamDescriptor Lz4MtStreamDescriptor;


struct Lz4MtContext {
	Lz4MtResult			result;
	void*				readCtx;
	Lz4MtRead			read;
	Lz4MtReadSkippable	readSkippable;
	Lz4MtReadSeek		readSeek;
	Lz4MtReadEof		readEof;
	void*				writeCtx;
	Lz4MtWrite			write;

	Lz4MtCompress		compress;
	Lz4MtCompressBound	compressBound;
	Lz4MtDecompress		decompress;
	Lz4MtMode			mode;
	int					compressionLevel;
};
typedef struct Lz4MtContext Lz4MtContext;


Lz4MtContext lz4mtInitContext();
Lz4MtStreamDescriptor lz4mtInitStreamDescriptor();
const char* lz4mtResultToString(Lz4MtResult result);
int lz4mtResultToLz4cExitCode(Lz4MtResult result);

Lz4MtResult lz4mtCompress(
	  Lz4MtContext* ctx
	, const Lz4MtStreamDescriptor* sd
);

Lz4MtResult lz4mtDecompress(
	  Lz4MtContext* ctx
	, Lz4MtStreamDescriptor* sd
);


#if defined (__cplusplus)
}
#endif

#endif // LZ4MT_H
