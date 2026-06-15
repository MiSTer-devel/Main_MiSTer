#include <cstdint>
#include <cassert>
#include <functional>
#include <memory>


/// Helper class to read .cdg files, containing CD+G data
/// and provide a way of reading the subchannel data in the
/// format cdrdao knows as raw_rw
class CdgUnpacker
{
  private:
	static constexpr size_t kCdgPackSize{24};
	static constexpr size_t kCdgPacksPerSector{4};
	static constexpr size_t kSubchannelSize{kCdgPacksPerSector * kCdgPackSize};

	std::unique_ptr<uint8_t[]> filebuffer_;
	size_t bufferlen_{0};
	size_t sectors_to_read_{0};
	int seek_offset{0};
	int sector_read_index_{0};

	/// Offsets for deinterleaving, thanks to the author of karaoke-dx
	std::array<int, 24> dffsets_ = {0,	18 - 24, 5 - 48,  23 - 72, 4 - 96,	2 - 120,  6 - 144,	7 - 168,
									8,	9 - 24,	 10 - 48, 11 - 72, 12 - 96, 13 - 120, 14 - 144, 15 - 168,
									16, 17 - 24, 1 - 48,  19 - 72, 20 - 96, 21 - 120, 22 - 144, 3 - 168};


  public:
	void CalculateBufferLength()
	{
		int max = 0;
		int min = 0;

		for (size_t sector = 0; sector < sectors_to_read_; sector++)
		{
			for (size_t pack = 0; pack < kCdgPacksPerSector; pack++)
			{
				for (size_t column = 0; column < kCdgPackSize; column++)
				{
					int offset = (sector * kSubchannelSize) + (pack * kCdgPackSize) + dffsets_[column];
					min = std::min(offset, min);
					max = std::max(offset, max);
				}
			}
		}
		assert(min < 0);
		assert(max > 0);

		bufferlen_ = max - min + 1;
		seek_offset = -min;
	}

	/// Prepare internal state to deliver subchannel data from the specified number of sectors
	/// A helper function must be provided to read the actual data
	void Seek(int lba, size_t sectors_to_read, std::function<void(uint8_t* buf, int offset, size_t len)> readhelper)
	{
		if (sectors_to_read_ != sectors_to_read)
		{
			sectors_to_read_ = sectors_to_read;
			CalculateBufferLength();
			filebuffer_ = std::make_unique<uint8_t[]>(bufferlen_);
		}

		// We need to clamp 0, because lba<=1 produces negative offsets
		int offset = std::max(0, static_cast<int>(kSubchannelSize * lba - seek_offset));

		readhelper(filebuffer_.get(), offset, bufferlen_);
		sector_read_index_ = 0;
	}

	/// Read one sector of subchannel RW data and advance to the next sector
	void ReadSectorSubchannelRw(std::array<uint8_t, kSubchannelSize>& b)
	{
		uint8_t* buf = b.data();
		for (size_t pack = 0; pack < kCdgPacksPerSector; pack++)
		{
			for (size_t column = 0; column < kCdgPackSize; column++)
			{
				int offset =
					(sector_read_index_ * kSubchannelSize) + (pack * kCdgPackSize) + dffsets_[column] + seek_offset;
				assert(offset >= 0);
				assert(offset < static_cast<int>(bufferlen_));
				*buf = filebuffer_[offset];
				buf++;
			}
		}
		sector_read_index_++;
	}
};