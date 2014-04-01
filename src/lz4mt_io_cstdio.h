#ifndef LZ4MT_IO_CSTDIO_H
#define LZ4MT_IO_CSTDIO_H

#include <string>
#include <cstdint>

struct Lz4MtContext;

namespace Lz4Mt { namespace Cstdio {

bool fileExist(const std::string& filename);
bool openIstream(Lz4MtContext* ctx, const std::string& filename);
bool openOstream(Lz4MtContext* ctx, const std::string& filename, bool nullWrite);
void closeIstream(Lz4MtContext* ctx);
void closeOstream(Lz4MtContext* ctx);
int read(Lz4MtContext* ctx, void* dst, int dstSize);
int readSkippable(const Lz4MtContext* ctx, uint32_t magicNumber, size_t size);
int readSeek(const Lz4MtContext* ctx, int offset);
int readEof(const Lz4MtContext* ctx);
int write(const Lz4MtContext* ctx, const void* source, int sourceSize);
uint64_t getFilesize(const std::string& filename);
std::string getStdinFilename();
std::string getStdoutFilename();
std::string getNullFilename();
bool isAttyStdin();
bool isAttyStdout();
bool compareFilename(const std::string& lhs, const std::string& rhs);
bool hasExtension(const std::string& filename, const std::string& extension);
std::string removeExtension(const std::string& filename);

}}

#endif
