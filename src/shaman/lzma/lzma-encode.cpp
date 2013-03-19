/**
 * @file lzma-encode.cpp
 *
 *  Created on: 12.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <lzma/LzmaEnc.h>
#include <shaman/lzma/lzma.hpp>

typedef shaman::lzma::detail::lzma_allocator< std::allocator< char > > default_allocator;

struct sz_alloc_impl : ISzAlloc {
	default_allocator alloc_;

	sz_alloc_impl()
	{
		Alloc =
			[](void* p, size_t size) -> void*
			{
				sz_alloc_impl* a = static_cast< sz_alloc_impl* >(p);
				return default_allocator::allocate( &a->alloc_, 1, size );
			};
		Free =
			[](void* p, void* address)
			{
				sz_alloc_impl* a = static_cast< sz_alloc_impl* >(p);
				default_allocator::deallocate(&a->alloc_, address);
			};
	}
};


struct LzmaInStream : public ISeqInStream {
	std::istream& stream;
	size_t max_read;

	SRes
	read(void* buf, size_t& size)
	{
		std::cerr << "Requested " << size << " bytes from input stream.";
		if (size > max_read)
			size = max_read;
		char* s = reinterpret_cast< char* >(buf);
		stream.read(s, size);
		size = stream.gcount();

		std::cerr << " Read " << size << " bytes.\n";

		return SZ_OK;
	}

	LzmaInStream( std::istream& in ) :
		stream(in),
		max_read(4096)
	{
		Read = []( void* p, void* buf, size_t* size ) -> SRes
		{
			LzmaInStream* st = reinterpret_cast<LzmaInStream*>(p);
			return st->read(buf, *size);
		};
	}
};

struct LzmaOutStream : public ISeqOutStream {
	std::ostream& stream;

	size_t
	write( void const* buf, size_t size )
	{
		std::cerr << "Write " << size << " bytes\n";
		char const* s = reinterpret_cast< char const* >(buf);
		stream.write(s, size);
		return size;
	}

	LzmaOutStream( std::ostream& out ) :
		stream(out)
	{
		Write = [](void* p, void const* buf, size_t size) -> size_t
		{
			LzmaOutStream* st = reinterpret_cast<LzmaOutStream*>(p);
			return st->write(buf, size);
		};
	}
};

void
encode(std::istream& in, std::ostream& out)
{
	size_t startPos = in.tellg();
	in.seekg(0, in.end);
	size_t totalSize = in.tellg();
	size_t inSize = 0;//totalSize - startPos;
	in.seekg(startPos, in.beg);

	std::cerr << "Encode size " << inSize << " (start from " << startPos
			<< ", total size " << totalSize << ")\n";

	CLzmaEncHandle enc;
	CLzmaEncProps props;

	sz_alloc_impl lz_alloc;

	LzmaInStream in_stream(in);
	LzmaOutStream out_stream(out);

	enc = LzmaEnc_Create(&lz_alloc);
	LzmaEncProps_Init(&props);
	SRes res = LzmaEnc_SetProps(enc, &props);
	if (res == SZ_OK) {
		unsigned char header[LZMA_PROPS_SIZE + 8];
		size_t headerSize = LZMA_PROPS_SIZE;

		res = LzmaEnc_WriteProperties(enc, header, &headerSize);
		for (int i = 0; i < 8; ++i) {
			header[headerSize++] = (unsigned char)( inSize >> (8 * i) );
		}
		out_stream.write(header, headerSize);
		res = LzmaEnc_Encode(enc, &out_stream, &in_stream, nullptr, &lz_alloc, &lz_alloc);
	}

	LzmaEnc_Destroy(enc, &lz_alloc, &lz_alloc);

	if (res != SZ_OK)
		throw std::runtime_error("LZMA error");
}

int
main(int argc, char* argv[]) {
	try {
		if (argc < 2) {
			std::cerr << "Usage " << argv[0] << " <filename>\n";
			return 1;
		}

		std::ifstream file(argv[1]);

		encode(file, std::cout);

	} catch ( std::exception const& e ) {
		std::cerr << "Standard exception: " << e.what() << "\n";
		return 1;
	} catch ( ... ) {
		std::cerr << "Unexpected exception\n";
		return 1;
	}
	return 0;
}
