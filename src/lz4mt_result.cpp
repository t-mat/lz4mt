#include "lz4mt.h"


extern "C" const char*
lz4mtResultToString(Lz4MtResult result)
{
	const char* s = "???";
	switch(result) {
	case LZ4MT_RESULT_OK:
		s = "OK";
		break;
	case LZ4MT_RESULT_ERROR:
		s = "ERROR";
		break;
	case LZ4MT_RESULT_INVALID_MAGIC_NUMBER:
		s = "INVALID_MAGIC_NUMBER";
		break;
	case LZ4MT_RESULT_INVALID_HEADER:
		s = "INVALID_HEADER";
		break;
	case LZ4MT_RESULT_PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET:
		s = "PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET";
		break;
	case LZ4MT_RESULT_BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET:
		s = "BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET";
		break;
	case LZ4MT_RESULT_INVALID_VERSION:
		s = "INVALID_VERSION";
		break;
	case LZ4MT_RESULT_INVALID_HEADER_CHECKSUM:
		s = "INVALID_HEADER_CHECKSUM";
		break;
	case LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE:
		s = "INVALID_BLOCK_MAXIMUM_SIZE";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_HEADER:
		s = "CANNOT_WRITE_HEADER";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_EOS:
		s = "CANNOT_WRITE_EOS";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM:
		s = "CANNOT_WRITE_STREAM_CHECKSUM";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE:
		s = "CANNOT_READ_BLOCK_SIZE";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA:
		s = "CANNOT_READ_BLOCK_DATA";
		break;
	case LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM:
		s = "CANNOT_READ_BLOCK_CHECKSUM";
		break;
	case LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM:
		s = "CANNOT_READ_STREAM_CHECKSUM";
		break;
	case LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH:
		s = "STREAM_CHECKSUM_MISMATCH";
		break;
	case LZ4MT_RESULT_DECOMPRESS_FAIL:
		s = "DECOMPRESS_FAIL";
		break;
	case LZ4MT_RESULT_BAD_ARG:
		s = "BAD_ARG";
		break;
	case LZ4MT_RESULT_INVALID_BLOCK_SIZE:
		s = "INVALID_BLOCK_SIZE";
		break;
	case LZ4MT_RESULT_INVALID_HEADER_RESERVED1:
		s = "INVALID_HEADER_RESERVED1";
		break;
	case LZ4MT_RESULT_INVALID_HEADER_RESERVED2:
		s = "INVALID_HEADER_RESERVED2";
		break;
	case LZ4MT_RESULT_INVALID_HEADER_RESERVED3:
		s = "INVALID_HEADER_RESERVED3";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK:
		s = "CANNOT_WRITE_DATA_BLOCK";
		break;
	case LZ4MT_RESULT_CANNOT_WRITE_DECODED_BLOCK:
		s = "CANNOT_WRITE_DECODED_BLOCK";
		break;
	default:
		s = "Unknown code";
		break;
	}
	return s;
}


extern "C" int
lz4mtResultToLz4cExitCode(Lz4MtResult result) {
	int e = 1;
	switch(result) {
	case LZ4MT_RESULT_OK:
		e = 0;
		break;

	case LZ4MT_RESULT_ERROR:
		e = 1;
		break;

	case LZ4MT_RESULT_INVALID_MAGIC_NUMBER:
		//	selectDecoder()
		//		if (ftell(finput) == MAGICNUMBER_SIZE) EXM_THROW(44,"Unrecognized header : file cannot be decoded");   // Wrong magic number at the beginning of 1st stream
		e = 44;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_SKIPPABLE_SIZE_UNREADABLE:
		//	selectDecoder()
		//		if (nbReadBytes != 4) EXM_THROW(42, "Stream error : skippable size unreadable");
		e = 42;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_CANNOT_SKIP_SKIPPABLE_AREA:
		//	selectDecoder()
		//		if (errorNb != 0) EXM_THROW(43, "Stream error : cannot skip skippable area");
		e = 43;
		break;

	case LZ4MT_RESULT_BLOCK_DEPENDENCE_IS_NOT_SUPPORTED_YET:
		e = 1;
		break;

	case LZ4MT_RESULT_CANNOT_WRITE_HEADER:
		//	compress_file_blockDependency()
		//	LZ4IO_compressFilename()
		//		if (sizeCheck!=header_size) EXM_THROW(32, "Write error : cannot write header");
		//
		e = 32;
		break;

	case LZ4MT_RESULT_CANNOT_WRITE_EOS:
		//	compress_file_blockDependency()
		//	LZ4IO_compressFilename()
		//		if (sizeCheck!=(size_t)(4)) EXM_THROW(37, "Write error : cannot write end of stream");
		//
		e = 37;
		break;

	case LZ4MT_RESULT_CANNOT_WRITE_STREAM_CHECKSUM:
		//	compress_file_blockDependency()
		//	LZ4IO_compressFilename()
		//		if (sizeCheck!=(size_t)(4)) EXM_THROW(37, "Write error : cannot write stream checksum");
		e = 37;
		break;

	case LZ4MT_RESULT_INVALID_HEADER:
		//	decodeLZ4S()
		//		if (nbReadBytes != 3) EXM_THROW(61, "Unreadable header");
		e = 61;
		break;

	case LZ4MT_RESULT_INVALID_VERSION:
		//decodeLZ4S()
		//		if (version != 1)       EXM_THROW(62, "Wrong version number");
		e = 62;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_RESERVED1:
		//decodeLZ4S()
		//		if (reserved1 != 0)     EXM_THROW(65, "Wrong value for reserved bits");
		e = 65;
		break;

	case LZ4MT_RESULT_PRESET_DICTIONARY_IS_NOT_SUPPORTED_YET:
		//	decodeLZ4S()
		//		if (dictionary == 1)    EXM_THROW(66, "Does not support dictionary");
		e = 66;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_RESERVED2:
		//decodeLZ4S()
		//		if (reserved2 != 0)     EXM_THROW(67, "Wrong value for reserved bits");
		e = 67;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_RESERVED3:
		//decodeLZ4S()
		//		if (reserved3 != 0)     EXM_THROW(67, "Wrong value for reserved bits");
		e = 67;
		break;

	case LZ4MT_RESULT_INVALID_BLOCK_MAXIMUM_SIZE:
		//	decodeLZ4S()
		//		if (blockSizeId < 4)    EXM_THROW(68, "Unsupported block size");
		e = 68;
		break;

	case LZ4MT_RESULT_INVALID_HEADER_CHECKSUM:
		//	decodeLZ4S()
		//		if (checkBits != checkBits_xxh32) EXM_THROW(69, "Stream descriptor error detected");
		e = 69;
		break;

	case LZ4MT_RESULT_CANNOT_READ_BLOCK_SIZE:
		//	decodeLZ4S()
		//		if( nbReadBytes != 4 ) EXM_THROW(71, "Read error : cannot read next block size");
		e = 71;
		break;

	case LZ4MT_RESULT_INVALID_BLOCK_SIZE:
		//	decodeLZ4S()
		//		if (blockSize > maxBlockSize) EXM_THROW(72, "Error : invalid block size");
		e = 72;
		break;

	case LZ4MT_RESULT_CANNOT_READ_BLOCK_DATA:
		//	decodeLZ4S()
		//		if( nbReadBytes != blockSize ) EXM_THROW(73, "Read error : cannot read data block" );
		e = 73;
		break;

	case LZ4MT_RESULT_CANNOT_READ_BLOCK_CHECKSUM:
		//	decodeLZ4S()
		//		if( sizeCheck != 4 ) EXM_THROW(74, "Read error : cannot read next block size");
		e = 74;
		break;

	case LZ4MT_RESULT_BLOCK_CHECKSUM_MISMATCH:
		//	decodeLZ4S()
		//		if (checksum != readChecksum) EXM_THROW(75, "Error : invalid block checksum detected");
		e = 75;
		break;

	case LZ4MT_RESULT_CANNOT_READ_STREAM_CHECKSUM:
		//	decodeLZ4S()
		//		if (sizeCheck != 4) EXM_THROW(74, "Read error : cannot read stream checksum");
		e = 74;
		break;

	case LZ4MT_RESULT_STREAM_CHECKSUM_MISMATCH:
		//	decodeLZ4S()
		//		if (checksum != readChecksum) EXM_THROW(75, "Error : invalid stream checksum detected");
		e = 75;
		break;

	case LZ4MT_RESULT_CANNOT_WRITE_DATA_BLOCK:
		//	cannot write incompressible block
		//	decodeLZ4S()
		//		if (sizeCheck != (size_t)blockSize) EXM_THROW(76, "Write error : cannot write data block");
		e = 76;
		break;

	case LZ4MT_RESULT_DECOMPRESS_FAIL:
		//	decodeLZ4S()
		//		if (decodedBytes < 0) EXM_THROW(77, "Decoding Failed ! Corrupted input detected !");
		e = 77;
		break;

	case LZ4MT_RESULT_CANNOT_WRITE_DECODED_BLOCK:
		//	decodeLZ4S()
		//		if (sizeCheck != (size_t)decodedBytes) EXM_THROW(78, "Write error : cannot write decoded block\n");
		e = 78;
		break;

	default:
		//	LZ4IO_compressFilename_Legacy()
		//		if (!in_buff || !out_buff) EXM_THROW(21, "Allocation error : not enough memory");
		//		if (sizeCheck!=MAGICNUMBER_SIZE) EXM_THROW(22, "Write error : cannot write header");
		//
		//	decodeLZ4S()
		//		if (streamSize == 1)    EXM_THROW(64, "Does not support stream size");
		//			NOTE : lz4mt support the streamSize header flag.
		e = 1;
		break;
	}
	return e;
}
