#ifdef __x86_64__
#include <cstdint>
#include <cstdio>
#include "cdg_unpacker.hpp"

int main()
{
	CdgUnpacker cdg;

	FILE* infile = fopen("Jimi Hendrix Experience - Smash Hits (USA) [bad].cdg", "rb");
	assert(infile);
	FILE* outfile = fopen("testout.int", "wb");
	assert(outfile);

	int sectors_to_read = 6;
	int lba = 0;

	for (int i = 0; i < 40; i++)
	{
		cdg.Seek(lba,
				 sectors_to_read,
				 [&](uint8_t* buf, int offset, size_t len)
				 {
					 fseek(infile, offset, SEEK_SET);
					 fread(buf, 1, len, infile);

					 //printf("read\n");
				 });

		std::array<uint8_t, 96> subchanbuf;

		for (int i = 0; i < sectors_to_read; i++)
		{
			cdg.ReadSectorSubchannelRw(subchanbuf);
			fwrite(subchanbuf.data(), subchanbuf.size(), 1, outfile);
		}
		lba += sectors_to_read;
	}
	fclose(infile);
	fclose(outfile);
}

// g++ cdg_unpacker_unittest.cpp  && ./a.out
#endif