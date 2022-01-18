//---------------------------------------------------------------------------
#include "DetailedResolutionClass.h"
#include <cstdio>
#include <cstring>
//---------------------------------------------------------------------------
const int DetailedResolutionClass::MinTiming = 0;
const int DetailedResolutionClass::MaxTiming = 5;

const char *DetailedResolutionClass::TimingText[] =
{
	"Manual",
	"Automatic - LCD standard",
	"Automatic - LCD native",
	"Automatic - LCD reduced",
	"Automatic - CRT standard",
	"Automatic - Old standard",
};


const int DetailedResolutionClass::LCDStandard[][13] =
{
	{3840, 2160, 0, 59500, 60500, 176,  88, 296,  8, 10, 72, 1, 1}, // 3840x2160 @ 60 Hz
	{3840, 2160, 0, 29500, 30500, 176,  88, 296,  8, 10, 72, 1, 1}, // 3840x2160 @ 30 Hz
	{1920, 1080, 0, 59500, 60500,  88,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 60 Hz
	{1920, 1080, 0, 47500, 50500, 528,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 50 Hz
//	{1920, 1080, 0, 47500, 48500, 638,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 48 Hz (non-standard)
	{1920, 1080, 0, 29500, 30500,  88,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 30 Hz
	{1920, 1080, 0, 24500, 25500, 528,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 25 Hz
	{1920, 1080, 0, 23500, 24500, 638,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 24 Hz
	{1920,  540, 1, 59500, 60500,  88,  44, 148,  2,  5, 15, 1, 1}, // 1920x1080i @ 60 Hz
	{1920,  540, 1, 47500, 50500, 528,  44, 148,  2,  5, 15, 1, 1}, // 1920x1080i @ 50 Hz
//	{1920,  540, 1, 47500, 48500, 638,  44, 148,  2,  5, 15, 1, 1}, // 1920x1080i @ 48 Hz (non-standard)
	{1440,  288, 1, 47500, 50500,  24, 126, 138,  2,  3, 19, 0, 0}, // 1440x576i @ 50 Hz
	{1440,  240, 1, 59500, 60500,  38, 124, 114,  4,  3, 15, 0, 0}, // 1440x480i @ 60 Hz
	{1366,  768, 0, 59500, 60500,  70, 143, 213,  3,  3, 24, 1, 1}, // 1366x768 @ 60 Hz
	{1360,  768, 0, 59500, 60500,  64, 112, 256,  3,  6, 18, 1, 1}, // 1360x768 @ 60 Hz
	{1280,  720, 0, 59500, 60500, 110,  40, 220,  5,  5, 20, 1, 1}, // 1280x720 @ 60 Hz
	{1280,  720, 0, 47500, 50500, 440,  40, 220,  5,  5, 20, 1, 1}, // 1280x720 @ 50 Hz
//	{1280,  720, 0, 47500, 48500, 110,  40, 220,  5,  5, 20, 1, 1}, // 1280x720 @ 48 Hz (non-standard)
	{ 720,  576, 0, 47500, 50500,  12,  64,  68,  5,  5, 39, 0, 0}, // 720x576 @ 50 Hz
	{ 720,  480, 0, 59500, 60500,  16,  62,  60,  9,  6, 30, 0, 0}, // 720x480 @ 60 Hz
	{ 640,  480, 0, 59500, 63500,  16,  96,  48, 10,  2, 33, 0, 0}, // 640x480 @ 60 Hz
	{0}
};

const int DetailedResolutionClass::LCDNative[][12] =
{
	{3840, 2160, 0, 60000, 176,  88, 296,  8, 10, 72, 1, 1}, // 3840x2160 @ 60 Hz
	{1920, 1080, 0, 60000,  88,  44, 148,  4,  5, 36, 1, 1}, // 1920x1080 @ 60 Hz
	{1920,  540, 1, 60000,  88,  44, 148,  2,  5, 15, 1, 1}, // 1920x1080i @ 60 Hz
	{1440,  288, 1, 50000,  24, 126, 138,  2,  3, 19, 0, 0}, // 1440x576i @ 50 Hz
	{1440,  240, 1, 59940,  38, 124, 114,  4,  3, 15, 0, 0}, // 1440x480i @ 60 Hz
	{1366,  768, 0, 59789,  70, 143, 213,  3,  3, 24, 1, 1}, // 1366x768 @ 60 Hz
	{1360,  768, 0, 60015,  64, 112, 256,  3,  6, 18, 1, 1}, // 1360x768 @ 60 Hz
	{1280,  720, 0, 60000, 110,  40, 220,  5,  5, 20, 1, 1}, // 1280x720 @ 60 Hz
	{ 720,  576, 0, 50000,  12,  64,  68,  5,  5, 39, 0, 0}, // 720x576 @ 50 Hz
	{ 720,  480, 0, 59940,  16,  62,  60,  9,  6, 30, 0, 0}, // 720x480 @ 60 Hz
	{ 640,  480, 0, 60000,  16,  96,  48, 10,  2, 33, 0, 0}, // 640x480 @ 60 Hz
	{0}
};

const int DetailedResolutionClass::LCDReduced[][13] =
{
	{0}
};

const int DetailedResolutionClass::CRTStandard[][13] =
{
	{0}
};

const int DetailedResolutionClass::OldStandard[][13] =
{
	{0}
};

// {0.98, 1.02, VSync} * VActive * 4000 / HActive
const int DetailedResolutionClass::AspectVSync[][3] =
{
	{2205, 2295, 5}, // 2250 (16:9)
	{2352, 2448, 7}, // 2400 (15:9)
	{2450, 2550, 6}, // 2500 (16:10)
	{2940, 3060, 4}, // 3000 (4:3)
	{3136, 3264, 7}, // 3200 (5:4)
	{0}
};

const int DetailedResolutionClass::C = 40;
const int DetailedResolutionClass::J = 20;
const int DetailedResolutionClass::K = 128;
const int DetailedResolutionClass::M = 600;

const int DetailedResolutionClass::CPrime = (C - J) * K / 256 + J;
const int DetailedResolutionClass::MPrime = M * K / 256;

const int DetailedResolutionClass::MinHActive[] = {1, 1};
const int DetailedResolutionClass::MaxHActive[] = {4095, 65536};
const int DetailedResolutionClass::MinHFront[] = {1, 1};
const int DetailedResolutionClass::MaxHFront[] = {1023, 32768};
const int DetailedResolutionClass::MinHSync[] = {1, 1};
const int DetailedResolutionClass::MaxHSync[] = {1023, 65536};
const int DetailedResolutionClass::MinHBack[] = {0, 0};
const int DetailedResolutionClass::MaxHBack[] = {4093, 65534};
const int DetailedResolutionClass::MinHBlank[] = {2, 2};
const int DetailedResolutionClass::MaxHBlank[] = {4095, 65536};
const int DetailedResolutionClass::MinHTotal[] = {3, 3};
const int DetailedResolutionClass::MaxHTotal[] = {8190, 131072};

const int DetailedResolutionClass::MinVActive[] = {1, 1};
const int DetailedResolutionClass::MaxVActive[] = {4095, 65536};
const int DetailedResolutionClass::MinVFront[] = {1, 1};
const int DetailedResolutionClass::MaxVFront[] = {63, 32768};
const int DetailedResolutionClass::MinVSync[] = {1, 1};
const int DetailedResolutionClass::MaxVSync[] = {63, 65536};
const int DetailedResolutionClass::MinVBack[] = {0, 0};
const int DetailedResolutionClass::MaxVBack[] = {4093, 65534};
const int DetailedResolutionClass::MinVBlank[] = {2, 2};
const int DetailedResolutionClass::MaxVBlank[] = {4095, 65536};
const int DetailedResolutionClass::MinVTotal[] = {3, 3};
const int DetailedResolutionClass::MaxVTotal[] = {8190, 131072};

const long long DetailedResolutionClass::MinVRate[] = {1, 1};
const long long DetailedResolutionClass::MaxVRate[] = {10000000, 10000000};
const long long DetailedResolutionClass::MinHRate[] = {1, 1};
const long long DetailedResolutionClass::MaxHRate[] = {10000000, 10000000};
const long long DetailedResolutionClass::MinPClock[] = {1, 1};
const long long DetailedResolutionClass::MaxPClock[] = {65535, 16777216};

const bool DetailedResolutionClass::InterlacedAvailable[] = {true, true};
const bool DetailedResolutionClass::NativeAvailable[] = {false, true};

bool DetailedResolutionClass::Copied;
int DetailedResolutionClass::CopyType;
int DetailedResolutionClass::CopyHActive;
int DetailedResolutionClass::CopyHFront;
int DetailedResolutionClass::CopyHSync;
int DetailedResolutionClass::CopyHBlank;
bool DetailedResolutionClass::CopyHPolarity;
int DetailedResolutionClass::CopyVActive;
int DetailedResolutionClass::CopyVFront;
int DetailedResolutionClass::CopyVSync;
int DetailedResolutionClass::CopyVBlank;
bool DetailedResolutionClass::CopyVPolarity;
int DetailedResolutionClass::CopyStereo;
long long DetailedResolutionClass::CopyPClock;
bool DetailedResolutionClass::CopyInterlaced;
bool DetailedResolutionClass::CopyNative;
//---------------------------------------------------------------------------
DetailedResolutionClass::DetailedResolutionClass(int NewType)
{
	Type = NewType;
	Timing = 4;
	Last = 0;
	HActive = BLANK;
	HFront = BLANK;
	HSync = BLANK;
	HBack = BLANK;
	HBlank = BLANK;
	HTotal = BLANK;
	HPolarity = false;
	VActive = BLANK;
	VFront = BLANK;
	VSync = BLANK;
	VBack = BLANK;
	VBlank = BLANK;
	VTotal = BLANK;
	VPolarity = false;
	Stereo = 0;
	LastRate = 0;
	VRate = ActualVRate = BLANK;
	HRate = ActualHRate = BLANK;
	PClock = BLANK;
	Interlaced = false;
	Native = false;
	VActiveI = BLANK;
	VFrontI = BLANK;
	VSyncI = BLANK;
	VBackI = BLANK;
	VBlankI = BLANK;
	VTotalI = BLANK;
	VRateI = BLANK;

	DASH = "-";

	ResetAvailable = false;
	ResetHActive = BLANK;
	ResetHFront = BLANK;
	ResetHSync = BLANK;
	ResetHBlank = BLANK;
	ResetHPolarity = false;
	ResetVActive = BLANK;
	ResetVFront = BLANK;
	ResetVSync = BLANK;
	ResetVBlank = BLANK;
	ResetVPolarity = false;
	ResetStereo = false;
	ResetPClock = BLANK;
	ResetInterlaced = false;
	ResetNative = false;
	Init();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::Init()
{
	long long OldPClock;

	HActive = 1920;
	HFront = 128;
	HSync = 200;
	HBlank = 656;
	HPolarity = false;
	VActive = 1080;
	VFront = 3;
	VSync = 5;
	VBlank = 40;
	VPolarity = true;
	Stereo = 0;
	PClock = 173.11;
	Interlaced = false;
	Native = false;

	CalculateHBack();
	CalculateHTotal();
	CalculateVBack();
	CalculateVTotal();
	CalculateActualVRate();
	CalculateActualHRate();
	VRate = (ActualVRate + 500) / 1000 * 1000;
	HRate = ActualHRate;
	OldPClock = PClock;
	CalculatePClockFromVRate();

	if (PClock != OldPClock)
	{
		if (VRate % 24000 == 0 || VRate % 30000 == 0)
		{
			VRate = VRate * 1000 / 1001;
			CalculatePClockFromVRate();
		}
	}

	if (PClock != OldPClock)
	{
		VRate = (ActualVRate + 50) / 100 * 100;
		CalculatePClockFromVRate();
	}

	if (PClock != OldPClock)
	{
		VRate = ActualVRate;
		PClock = OldPClock;
	}

	UpdateInterlaced();
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetType(int NewType)
{
	Type = NewType;
	return true;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetTiming()
{
	if (!IsValidTiming())
		return -1;

	return Timing;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetTiming(int Value)
{
	Timing = Value;
	Update();
	UpdateInterlaced();
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsLast(int Value)
{
	return Last == Value;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHActive()
{
	return HActive;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHFront()
{
	return HFront;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHSync()
{
	return HSync;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBack()
{
	return HBack;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBlank()
{
	return HBlank;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHTotal()
{
	return HTotal;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::GetHPolarity()
{
	return HPolarity;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVActive()
{
	return VActive;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVFront()
{
	return VFront;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVSync()
{
	return VSync;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVBack()
{
	return VBack;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVBlank()
{
	return VBlank;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVTotal()
{
	return VTotal;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::GetVPolarity()
{
	return VPolarity;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsLastRate(int Value)
{
	return LastRate == Value;
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetVRate()
{
	return VRate;
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetHRate()
{
	return HRate;
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetPClock()
{
	return PClock;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetLast(int Value)
{
	Last = Value;
	Timing = 0;
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHActive(int Value)
{
	HActive = Value;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHFront(int Value)
{
	HFront = Value;
	Timing = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHSync(int Value)
{
	HSync = Value;
	Timing = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHBack(int Value)
{
	HBack = Value;
	Timing = 0;
	Last = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHBlank(int Value)
{
	HBlank = Value;
	Timing = 0;
	Last = 1;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHTotal(int Value)
{
	HTotal = Value;
	Timing = 0;
	Last = 2;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHPolarity(bool Value)
{
	HPolarity = Value;
	Timing = 0;
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVActive(int Value)
{
	VActive = Value;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVFront(int Value)
{
	VFront = Value;
	Timing = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVSync(int Value)
{
	VSync = Value;
	Timing = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVBack(int Value)
{
	VBack = Value;
	Timing = 0;
	Last = 0;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVBlank(int Value)
{
	VBlank = Value;
	Timing = 0;
	Last = 1;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVTotal(int Value)
{
	VTotal = Value;
	Timing = 0;
	Last = 2;
	Update();
	UpdateInterlaced();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVPolarity(bool Value)
{
	VPolarity = Value;
	Timing = 0;
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetLastRate(int Value)
{
	LastRate = Value;
	Timing = 0;
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetVRate(long long Value)
{
	VRate = Value;

	if (Timing == 0)
		LastRate = 0;

	Update();
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetHRate(long long Value)
{
	HRate = Value;
	LastRate = 1;
	Update();
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetPClock(long long Value)
{
	PClock = Value;
	LastRate = 2;
	Update();
	UpdateInterlacedRate();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::InterlacedPossible()
{
	return InterlacedAvailable[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::GetInterlaced()
{
	return Interlaced;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetInterlaced(bool Value)
{
	Interlaced = Value;

	VActive ^= VActiveI;
	VActiveI ^= VActive;
	VActive ^= VActiveI;

	VFront ^= VFrontI;
	VFrontI ^= VFront;
	VFront ^= VFrontI;

	VSync ^= VSyncI;
	VSyncI ^= VSync;
	VSync ^= VSyncI;

	VBack ^= VBackI;
	VBackI ^= VBack;
	VBack ^= VBackI;

	VBlank ^= VBlankI;
	VBlankI ^= VBlank;
	VBlank ^= VBlankI;

	VTotal ^= VTotalI;
	VTotalI ^= VTotal;
	VTotal ^= VTotalI;

	VRate ^= VRateI;
	VRateI ^= VRate;
	VRate ^= VRateI;

	Update();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::NativePossible()
{
	return NativeAvailable[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::GetNative()
{
	return Native;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::SetNative(bool Value)
{
	Native = Value;
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::Update()
{
	if (Timing)
	{
		if (!IsValidTiming())
		{
			HFront = HSync = HBack = HBlank = HTotal = BLANK;
			VFront = VSync = VBack = VBlank = VTotal = BLANK;
			PClock = ActualVRate = ActualHRate = HRate = BLANK;
			return false;
		}

		return true;
	}

	switch (Last)
	{
		case 0:
			CalculateHBlank();
			CalculateHTotal();
			CalculateVBlank();
			CalculateVTotal();
			break;

		case 1:
			CalculateHBack();
			CalculateHTotal();
			CalculateVBack();
			CalculateVTotal();
			break;

		case 2:
			CalculateHBackFromHTotal();
			CalculateHBlank();
			CalculateVBackFromVTotal();
			CalculateVBlank();
			break;
	}

	switch (LastRate)
	{
		case 0:
			CalculatePClockFromVRate();
			CalculateActualVRate();
			CalculateActualHRate();
			HRate = ActualHRate;
			break;

		case 1:
			CalculatePClockFromHRate();
			CalculateActualVRate();
			CalculateActualHRate();
			VRate = ActualVRate;
			break;

		case 2:
			CalculateActualVRate();
			CalculateActualHRate();
			VRate = ActualVRate;
			HRate = ActualHRate;
			break;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::UpdateInterlaced()
{
	VActiveI = VActive;
	VFrontI = VFront;
	VSyncI = VSync;
	VBackI = VBack;

	if (IsSupportedVActive())
	{
		if (Interlaced)
		{
			if (VActive == 540 && VFront == 2 && VSync == 5 && VBack == 15)
			{
				VActiveI = 1080;
				VFrontI = 4;
				VSyncI = 5;
				VBackI = 36;
			}
			else
			{
				if (VActive <= MaxVActive[Type] / 2)
					VActiveI = VActive * 2;
			}
		}
		else if (VActive % 2 == 0)
		{
			if (VActive == 1080 && VFront == 4 && VSync == 5 && VBack == 36)
			{
				VActiveI = 540;
				VFrontI = 2;
				VSyncI = 5;
				VBackI = 15;
			}
			else if (IsSupportedHActive())
			{
				if ((VActive * 125 > HActive * 51) || ((HActive == 1440 || HActive == 2880) && ((VActive >= 472 && VActive <= 488) || (VActive >= 566 && VActive <= 586))))
					VActiveI = VActive / 2;
			}
			else
			{
				if ((VActive >= 472 && VActive <= 488) || VActive >= 566)
					VActiveI = VActive / 2;
			}
		}
	}

	VBlankI = VFrontI + VSyncI + VBackI;
	VTotalI = VActiveI + VBlankI;
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::UpdateInterlacedRate()
{
	VRateI = VRate;

	if (IsSupportedVRate())
		if (!Interlaced)
			if (VRate < 45000)
				VRateI = VRate * 2;

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateNative(bool Digital)
{
	int Index;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; LCDNative[Index][0] != 0; Index++)
		if (HActive == LCDNative[Index][0] && VActive == LCDNative[Index][1] && Interlaced == LCDNative[Index][2])
			break;

	if (LCDNative[Index][0] != 0)
	{
		VRate = LCDNative[Index][3];
		HFront = LCDNative[Index][4];
		HSync = LCDNative[Index][5];
		HBack = LCDNative[Index][6];
		VFront = LCDNative[Index][7];
		VSync = LCDNative[Index][8];
		VBack = LCDNative[Index][9];
		HPolarity = LCDNative[Index][10];
		VPolarity = LCDNative[Index][11];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		VRate = 60000;

		if (Digital || HSync <= 48)
			CalculateLCDStandard();
		else
			CalculateCRTStandard();

		PClock = PClock / 25 * 25;
	}

	Stereo = 0;
	CalculateActualVRate();
	CalculateActualHRate();
	VRate = ActualVRate;
	HRate = ActualHRate;
	UpdateInterlaced();
	UpdateInterlacedRate();
	//UpdateReset();
	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateLCDStandard()
{
	int Index;
	long long OldVRate;

	HPolarity = true;
	VPolarity = false;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; LCDStandard[Index][0] != 0; Index++)
		if (HActive == LCDStandard[Index][0] && VActive == LCDStandard[Index][1] && Interlaced == LCDStandard[Index][2])
		if (VRate >= LCDStandard[Index][3] && VRate <= LCDStandard[Index][4])
			break;

	if (LCDStandard[Index][0] != 0)
	{
		HFront = LCDStandard[Index][5];
		HSync = LCDStandard[Index][6];
		HBack = LCDStandard[Index][7];
		VFront = LCDStandard[Index][8];
		VSync = LCDStandard[Index][9];
		VBack = LCDStandard[Index][10];
		HPolarity = LCDStandard[Index][11];
		VPolarity = LCDStandard[Index][12];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		OldVRate = VRate;
		CalculateCVTRB();
		CalculatePClockFromVRate();
		CalculateActualVRate();
		VRate = ActualVRate;
		CalculateCVTRB();
		CalculatePClockFromVRate();
		VRate = OldVRate;
	}

	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateLCDNative()
{
	int Index;
	long long OldVRate;

	HPolarity = true;
	VPolarity = false;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; LCDNative[Index][0] != 0; Index++)
		if (HActive == LCDNative[Index][0] && VActive == LCDNative[Index][1] && Interlaced == LCDNative[Index][2])
			break;

	if (LCDNative[Index][0] != 0)
	{
		HFront = LCDNative[Index][4];
		HSync = LCDNative[Index][5];
		HBack = LCDNative[Index][6];
		VFront = LCDNative[Index][7];
		VSync = LCDNative[Index][8];
		VBack = LCDNative[Index][9];
		HPolarity = LCDNative[Index][10];
		VPolarity = LCDNative[Index][11];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		OldVRate = VRate;
		VRate = 60000;
		CalculateCVTRB();
		CalculatePClockFromVRate();
		CalculateActualVRate();
		VRate = ActualVRate;
		CalculateCVTRB();
		VRate = OldVRate;
		CalculatePClockFromVRate();
	}

	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateLCDReduced()
{
	int Index;
	long long OldVRate;

	HPolarity = true;
	VPolarity = false;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; LCDReduced[Index][0] != 0; Index++)
		if (HActive == LCDReduced[Index][0] && VActive == LCDReduced[Index][1] && Interlaced == LCDReduced[Index][2])
		if (VRate >= LCDReduced[Index][3] && VRate <= LCDReduced[Index][4])
			break;

	if (LCDReduced[Index][0] != 0)
	{
		HFront = LCDReduced[Index][5];
		HSync = LCDReduced[Index][6];
		HBack = LCDReduced[Index][7];
		VFront = LCDReduced[Index][8];
		VSync = LCDReduced[Index][9];
		VBack = LCDReduced[Index][10];
		HPolarity = LCDReduced[Index][11];
		VPolarity = LCDReduced[Index][12];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		OldVRate = VRate;
		CalculateCVTRB();
		CalculatePClockFromVRate();
		CalculateActualVRate();
		VRate = ActualVRate;
		CalculateCVTRB();
		CalculatePClockFromVRate();
		VRate = OldVRate;

		if (VRate > 60500)
		{
			if (HActive * VActive > 2457600)
			{
				while (PClock > 33000)
				{
					if (VBlank > 15)
						VBack--;
					else if (HBack > 48)
					{
						HBack -= 8;
						VBack = GetVBackForCVTRB();
					}
					else if (VFront >= VSync && VFront >= VBack - 1 && VFront > 3)
						VFront--;
					else if (VSync >= VFront && VSync >= VBack && VSync > 3)
						VSync--;
					else if (VBack >= VFront && VBack >= VSync && VBack > 3)
						VBack--;
					else
						break;

					CalculateHBlank();
					CalculateHTotal();
					CalculateVBlank();
					CalculateVTotal();
					CalculatePClockFromVRate();
				}

				if (PClock > 33000)
				{
					OldVRate = VRate;
					CalculateCVTRB();
					CalculatePClockFromVRate();
					CalculateActualVRate();
					VRate = ActualVRate;
					CalculateCVTRB();
					CalculatePClockFromVRate();
					VRate = OldVRate;
				}

				while (PClock > 40000)
				{
					if (VBlank > 21)
						VBack--;
					else if (HBack > 56)
					{
						HBack -= 8;
						VBack = GetVBackForCVTRB();
					}
					else
						break;

					CalculateHBlank();
					CalculateHTotal();
					CalculateVBlank();
					CalculateVTotal();
					CalculatePClockFromVRate();
				}

				if (PClock > 40000)
				{
					OldVRate = VRate;
					CalculateCVTRB();
					CalculatePClockFromVRate();
					CalculateActualVRate();
					VRate = ActualVRate;
					CalculateCVTRB();
					CalculatePClockFromVRate();
					VRate = OldVRate;
				}

				while (PClock > 40400)
				{
					if (VBlank > 21)
						VBack--;
					else if (HBack > 56)
					{
						HBack -= 8;
						VBack = GetVBackForCVTRB();
					}
					else
						break;

					CalculateHBlank();
					CalculateHTotal();
					CalculateVBlank();
					CalculateVTotal();
					CalculatePClockFromVRate();
				}

				if (HActive * VActive > 3686400)
				{
					if (PClock > 40400)
					{
						HFront = 48;
						HSync = 32;
						HBack = 48;
						VFront = 3;
						VSync = 3;
						VBack = 3;
						CalculateHBlank();
						CalculateHTotal();
						CalculateVBlank();
						CalculateVTotal();
						CalculatePClockFromVRate();
					}

					if (PClock > 54000)
					{
						HFront = 16;
						HSync = 24;
						HBack = 24;
						VFront = 3;
						VSync = 3;
						VBack = 3;
						HPolarity = true;
						VPolarity = true;
						CalculateHBlank();
						CalculateHTotal();
						CalculateVBlank();
						CalculateVTotal();
						CalculatePClockFromVRate();
					}
				}
				else
				{
					if (PClock > 40400)
					{
						HFront = 48;
						HSync = 32;
						HBack = 64;
						VFront = 2;
						VSync = 2;
						VBack = 2;
						CalculateHBlank();
						CalculateHTotal();
						CalculateVBlank();
						CalculateVTotal();
						CalculatePClockFromVRate();
					}

					if (PClock > 54000)
					{
						HFront = 4;
						HSync = 16;
						HBack = 2;
						VFront = 1;
						VSync = 1;
						VBack = 7;
						HPolarity = true;
						VPolarity = true;
						CalculateHBlank();
						CalculateHTotal();
						CalculateVBlank();
						CalculateVTotal();
						CalculatePClockFromVRate();
					}
				}
			}
			else if (PClock > 16500)
			{
				if (HActive == 1920 && VActive == 1080)
				{
					HPolarity = true;
					VPolarity = true;
				}

				HFront = 32;
				HSync = 40;
				HBack = 48;
				VFront = GetVFrontForCVT();
				VSync = GetVSyncForCVT();
				VBack = GetVSyncForCVT();
				CalculateHBlank();
				CalculateHTotal();
				CalculateVBlank();
				CalculateVTotal();
				CalculatePClockFromVRate();

				while (PClock > 16500)
				{
					if (HFront >= HSync && HFront >= HBack - 8 && HFront > 8)
						HFront -= 8;
					else if (HSync >= HFront && HSync >= HBack && HSync > 8)
						HSync -= 8;
					else if (HBack >= HFront && HBack >= HSync && HBack > 8)
						HBack -= 8;
					else if (VFront >= VSync && VFront >= VBack - 1 && VFront > 3)
						VFront--;
					else if (VSync >= VFront && VSync >= VBack && VSync > 3)
						VSync--;
					else if (VBack >= VFront && VBack >= VSync && VBack > 3)
						VBack--;
					else
						break;

					CalculateHBlank();
					CalculateHTotal();
					CalculateVBlank();
					CalculateVTotal();
					CalculatePClockFromVRate();
				}

				if (PClock > 16500)
				{
					HFront = 24;
					HSync = 32;
					HBack = 32;
					VFront = GetVFrontForCVT();
					VSync = GetVSyncForCVT();
					VBack = GetVSyncForCVT() * 2;
					CalculateHBlank();
					CalculateHTotal();
					CalculateVBlank();
					CalculateVTotal();
					CalculatePClockFromVRate();
				}
			}
		}
	}

	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateCRTStandard()
{
	int Index;
	long long OldVRate;

	HPolarity = false;
	VPolarity = true;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; CRTStandard[Index][0] != 0; Index++)
		if (HActive == CRTStandard[Index][0] && VActive == CRTStandard[Index][1] && Interlaced == CRTStandard[Index][2])
		if (VRate >= CRTStandard[Index][3] && VRate <= CRTStandard[Index][4])
			break;

	if (CRTStandard[Index][0] != 0)
	{
		HFront = CRTStandard[Index][5];
		HSync = CRTStandard[Index][6];
		HBack = CRTStandard[Index][7];
		VFront = CRTStandard[Index][8];
		VSync = CRTStandard[Index][9];
		VBack = CRTStandard[Index][10];
		HPolarity = CRTStandard[Index][11];
		VPolarity = CRTStandard[Index][12];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		OldVRate = VRate;
		CalculateCVT();
		CalculatePClockFromVRate();
		CalculateActualVRate();
		VRate = ActualVRate;
		CalculateCVT();
		CalculatePClockFromVRate();
		VRate = OldVRate;
	}

	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateOldStandard()
{
	int Index;
	long long OldVRate;

	HPolarity = false;
	VPolarity = true;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	for (Index = 0; OldStandard[Index][0] != 0; Index++)
		if (HActive == OldStandard[Index][0] && VActive == OldStandard[Index][1] && Interlaced == OldStandard[Index][2])
		if (VRate >= OldStandard[Index][3] && VRate <= OldStandard[Index][4])
			break;

	if (OldStandard[Index][0] != 0)
	{
		HFront = OldStandard[Index][5];
		HSync = OldStandard[Index][6];
		HBack = OldStandard[Index][7];
		VFront = OldStandard[Index][8];
		VSync = OldStandard[Index][9];
		VBack = OldStandard[Index][10];
		HPolarity = OldStandard[Index][11];
		VPolarity = OldStandard[Index][12];
		CalculateHBlank();
		CalculateHTotal();
		CalculateVBlank();
		CalculateVTotal();
		CalculatePClockFromVRate();
	}
	else
	{
		OldVRate = VRate;
		CalculateGTF();
		CalculatePClockFromVRate();
		CalculateActualVRate();
		VRate = ActualVRate;
		CalculateGTF();
		CalculatePClockFromVRate();
		VRate = OldVRate;
	}

	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateCVT()
{
	HPolarity = false;
	VPolarity = true;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	HFront = GetHFrontForCVT();
	HSync = GetHSyncForCVT();
	HBack = GetHBackForCVT();
	VFront = GetVFrontForCVT();
	VSync = GetVSyncForCVT();
	VBack = GetVBackForCVT();
	CalculateHBlank();
	CalculateHTotal();
	CalculateVBlank();
	CalculateVTotal();
	CalculatePClockForCVT();
	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateCVTRB()
{
	HPolarity = true;
	VPolarity = false;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	HFront = 48;
	HSync = 32;
	HBack = 80;
	VFront = GetVFrontForCVT();
	VSync = GetVSyncForCVT();
	VBack = GetVBackForCVTRB();
	CalculateHBlank();
	CalculateHTotal();
	CalculateVBlank();
	CalculateVTotal();
	CalculatePClockForCVTRB();
	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateGTF()
{
	HPolarity = false;
	VPolarity = true;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return false;

	HFront = GetHFrontForGTF();
	HSync = GetHSyncForGTF();
	HBack = GetHBackForGTF();
	VFront = GetVFrontForGTF();
	VSync = GetVSyncForGTF();
	VBack = GetVBackForGTF();
	CalculateHBlank();
	CalculateHTotal();
	CalculateVBlank();
	CalculateVTotal();
	CalculatePClockForGTF();
	CalculateActualVRate();
	CalculateActualHRate();
	HRate = ActualHRate;
	return IsValidRate();
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetHPeriodForCVT()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return (1000000000000000000LL * 2 / VRate - 550000000000LL * 2) / (VActive * 2 + GetVFrontForCVT() * 2 + Interlaced);
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetHPeriodForCVTRB()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return (1000000000000000000LL * 2 / VRate - 460000000000LL * 2) / (VActive * 2);
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::GetHPeriodForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return (1000000000000000000LL * 2 / VRate - 550000000000LL * 2) / (VActive * 2 + GetVFrontForGTF() * 2 + Interlaced);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHFrontForCVT()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return GetHBackForCVT() - GetHSyncForCVT();
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHFrontForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return GetHBackForGTF() - GetHSyncForGTF();
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHSyncForCVT()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return (HActive + GetHBlankForCVT()) / 100 * 8;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHSyncForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return (HActive + GetHBlankForGTF() + 50) / 100 * 8;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBackForCVT()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return GetHBlankForCVT() / 2;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBackForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return GetHBlankForGTF() / 2;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBlankForCVT()
{
	long long IdealDutyCycle;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	IdealDutyCycle = CPrime * 1000000000000LL - MPrime * GetHPeriodForCVT();

	if (IdealDutyCycle < 20000000000000LL)
		IdealDutyCycle = 20000000000000LL;

	return HActive * IdealDutyCycle / (100000000000000LL - IdealDutyCycle) / 16 * 16;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetHBlankForGTF()
{
	long long IdealDutyCycle;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	IdealDutyCycle = CPrime * 1000000000000LL - MPrime * GetHPeriodForGTF();
/*
	if (IdealDutyCycle < 20000000000000LL)
		IdealDutyCycle = 20000000000000LL;
*/
	return (HActive * IdealDutyCycle / (100000000000000LL - IdealDutyCycle) + 8) / 16 * 16;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVFrontForCVT()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return 3;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVFrontForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return 1;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVSyncForCVT()
{
	int Aspect;
	int Index;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	if (Interlaced)
		Aspect = VActive * 8000 / HActive;
	else
		Aspect = VActive * 4000 / HActive;

	for (Index = 0; AspectVSync[Index][0] != 0; Index++)
		if (Aspect >= AspectVSync[Index][0] && Aspect <= AspectVSync[Index][1])
			return AspectVSync[Index][2];

	return 10;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVSyncForGTF()
{
	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	return 3;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVBackForCVT()
{
	int VSyncVBack;
	int VBack;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	VSyncVBack = 550000000000LL / GetHPeriodForCVT() + 1;
	VBack = VSyncVBack - GetVSyncForCVT();

	if (VBack < 6)
		VBack = 6;

	return VBack;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVBackForCVTRB()
{
	int VBlank;
	int VBack;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	VBlank = 460000000000LL / GetHPeriodForCVTRB() + 1;
	VBack = VBlank - GetVFrontForCVT() - GetVSyncForCVT();

	if (VBack < 6)
		VBack = 6;

	return VBack;
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetVBackForGTF()
{
	int VSyncVBack;
	int VBack;

	if (!IsSupportedHActive() || !IsSupportedVActive() || !IsSupportedVRate())
		return BLANK;

	VSyncVBack = (5500000000000LL / GetHPeriodForGTF() + 5) / 10;
	VBack = VSyncVBack - GetVSyncForGTF();
/*
	if (VBack < 6)
		VBack = 6;
*/
	return VBack;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateHBack()
{
	if (!IsSupportedHBlank() || !IsSupportedHFront() || !IsSupportedHSync())
	{
		HBack = BLANK;
		return false;
	}

	HBack = HBlank - HFront - HSync;

	if (!IsSupportedHBack())
	{
		HBack = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateHBackFromHTotal()
{
	if (!IsSupportedHTotal() || !IsSupportedHActive() || !IsSupportedHFront() || !IsSupportedHSync())
	{
		HBack = BLANK;
		return false;
	}

	HBack = HTotal - HActive - HFront - HSync;

	if (!IsSupportedHBack())
	{
		HBack = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateHBlank()
{
	if (!IsSupportedHFront() || !IsSupportedHSync() || !IsSupportedHBack())
	{
		HBlank = BLANK;
		return false;
	}

	HBlank = HFront + HSync + HBack;

	if (!IsSupportedHBlank())
	{
		HBlank = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateHTotal()
{
	if (!IsSupportedHActive() || !IsSupportedHFront() || !IsSupportedHSync() || !IsSupportedHBack())
	{
		HTotal = BLANK;
		return false;
	}

	HTotal = HActive + HFront + HSync + HBack;

	if (!IsSupportedHTotal())
	{
		HTotal = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateVBack()
{
	if (!IsSupportedVBlank() || !IsSupportedVFront() || !IsSupportedVSync())
	{
		VBack = BLANK;
		return false;
	}

	VBack = VBlank - VFront - VSync;

	if (!IsSupportedVBack())
	{
		VBack = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateVBackFromVTotal()
{
	if (!IsSupportedVTotal() || !IsSupportedVActive() || !IsSupportedVFront() || !IsSupportedVSync())
	{
		VBack = BLANK;
		return false;
	}

	VBack = VTotal - VActive - VFront - VSync;

	if (!IsSupportedVBack())
	{
		VBack = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateVBlank()
{
	if (!IsSupportedVFront() || !IsSupportedVSync() || !IsSupportedVBack())
	{
		VBlank = BLANK;
		return false;
	}

	VBlank = VFront + VSync + VBack;

	if (!IsSupportedVBlank())
	{
		VBlank = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateVTotal()
{
	if (!IsSupportedVActive() || !IsSupportedVFront() || !IsSupportedVSync() || !IsSupportedVBack())
	{
		VTotal = BLANK;
		return false;
	}

	VTotal = VActive + VFront + VSync + VBack;

	if (!IsSupportedVTotal())
	{
		VTotal = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculatePClockForCVT()
{
	if (!IsSupportedVRate() || !IsSupportedHTotal() || !IsSupportedVTotal())
	{
		PClock = BLANK;
		return false;
	}

	PClock = HTotal * 100000000000LL / GetHPeriodForCVT() / 25 * 25;

	if (!IsSupportedPClock())
	{
		PClock = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculatePClockForCVTRB()
{
	if (!IsSupportedVRate() || !IsSupportedHTotal() || !IsSupportedVTotal())
	{
		PClock = BLANK;
		return false;
	}

	PClock = VRate * HTotal * (VTotal * 2 + Interlaced) / 20000000 / 25 * 25;

	if (!IsSupportedPClock())
	{
		PClock = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculatePClockForGTF()
{
	if (!IsSupportedVRate() || !IsSupportedHTotal() || !IsSupportedVTotal())
	{
		PClock = BLANK;
		return false;
	}

	PClock = (VRate * HTotal * (VTotal * 2 + Interlaced) + 10000000) / 20000000;

	if (!IsSupportedPClock())
	{
		PClock = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculatePClockFromVRate()
{
	if (!IsSupportedVRate() || !IsSupportedHTotal() || !IsSupportedVTotal())
	{
		PClock = BLANK;
		return false;
	}

	PClock = (VRate * HTotal * (VTotal * 2 + Interlaced) + 19999999) / 20000000;

	if (!IsSupportedPClock())
	{
		PClock = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculatePClockFromHRate()
{
	if (!IsSupportedHRate() || !IsSupportedHTotal())
	{
		PClock = BLANK;
		return false;
	}

	PClock = (HRate * HTotal + 9999) / 10000;

	if (!IsSupportedPClock())
	{
		PClock = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateActualVRate()
{
	if (!IsSupportedPClock() || !IsSupportedHTotal() || !IsSupportedVTotal())
	{
		ActualVRate = BLANK;
		return false;
	}

	ActualVRate = PClock * 20000000 / HTotal / (VTotal * 2 + Interlaced);

	if (!IsSupportedActualVRate())
	{
		ActualVRate = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::CalculateActualHRate()
{
	if (!IsSupportedPClock() || !IsSupportedHTotal())
	{
		ActualHRate = BLANK;
		return false;
	}

	ActualHRate = PClock * 10000 / HTotal;

	if (!IsSupportedActualHRate())
	{
		ActualHRate = BLANK;
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValid()
{
	if (IsValidHActive())
	if (IsValidHFront())
	if (IsValidHSync())
	if (IsValidHBack())
	if (IsValidHBlank())
	if (IsValidHTotal())
	if (IsValidVActive())
	if (IsValidVFront())
	if (IsValidVSync())
	if (IsValidVBack())
	if (IsValidVBlank())
	if (IsValidVTotal())
	if (IsValidVRate())
	if (IsValidHRate())
	if (IsValidPClock())
	if (IsValidActualVRate())
	if (IsValidActualHRate())
		return true;

	return false;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidTiming()
{
	return Timing >= MinTiming && Timing <= MaxTiming;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHActive()
{
	return HActive >= MinHActive[Type] && HActive <= MaxHActive[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHFront()
{
	return HFront >= MinHFront[Type] && HFront <= MaxHFront[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHSync()
{
	return HSync >= MinHSync[Type] && HSync <= MaxHSync[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHBack()
{
	return HBack >= GetMinHBack(Type) && HBack <= GetMaxHBack(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHBlank()
{
	return HBlank >= GetMinHBlank(Type) && HBlank <= GetMaxHBlank(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHTotal()
{
	return HTotal >= GetMinHTotal(Type) && HTotal <= GetMaxHTotal(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVActive()
{
	return VActive >= MinVActive[Type] && VActive <= MaxVActive[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVFront()
{
	return VFront >= MinVFront[Type] && VFront <= MaxVFront[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVSync()
{
	return VSync >= MinVSync[Type] && VSync <= MaxVSync[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVBack()
{
	return VBack >= GetMinVBack(Type) && VBack <= GetMaxVBack(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVBlank()
{
	return VBlank >= GetMinVBlank(Type) && VBlank <= GetMaxVBlank(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVTotal()
{
	return VTotal >= GetMinVTotal(Type) && VTotal <= GetMaxVTotal(Type);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidRate()
{
	if (Timing == 0)
	{
		if (LastRate == 0)
		{
			if (!IsValidHTotal() || !IsValidVTotal())
				return IsValidVRate();
		}
		else if (LastRate == 1)
		{
			if (!IsValidHTotal())
				return IsValidHRate();
		}
		else if (LastRate == 2)
		{
			if (!IsValidHTotal() || !IsValidVTotal())
				return IsValidPClock();
		}
	}
	else
	{
		if (!IsValidHActive() || !IsValidVActive())
			return IsValidVRate();
	}

	return IsValidVRate() && IsValidHRate() && IsValidPClock() && IsValidActualVRate() && IsValidActualHRate();
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidVRate()
{
	return VRate >= MinVRate[Type] && VRate <= MaxVRate[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidHRate()
{
	return HRate >= MinHRate[Type] && HRate <= MaxHRate[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidPClock()
{
	return PClock >= MinPClock[Type] && PClock <= MaxPClock[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidActualVRate()
{
	return ActualVRate >= MinVRate[Type] && ActualVRate <= MaxVRate[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsValidActualHRate()
{
	return ActualHRate >= MinHRate[Type] && ActualHRate <= MaxHRate[Type];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupported()
{
	if (IsSupportedHActive())
	if (IsSupportedHFront())
	if (IsSupportedHSync())
	if (IsSupportedHBack())
	if (IsSupportedHBlank())
	if (IsSupportedHTotal())
	if (IsSupportedVActive())
	if (IsSupportedVFront())
	if (IsSupportedVSync())
	if (IsSupportedVBack())
	if (IsSupportedVBlank())
	if (IsSupportedVTotal())
	if (IsSupportedVRate())
	if (IsSupportedHRate())
	if (IsSupportedPClock())
	if (IsSupportedActualVRate())
	if (IsSupportedActualHRate())
		return true;

	return false;
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHActive()
{
	return HActive >= MinHActive[1] && HActive <= MaxHActive[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHFront()
{
	return HFront >= MinHFront[1] && HFront <= MaxHFront[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHSync()
{
	return HSync >= MinHSync[1] && HSync <= MaxHSync[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHBack()
{
	return HBack >= GetMinHBack(1) && HBack <= GetMaxHBack(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHBlank()
{
	return HBlank >= GetMinHBlank(1) && HBlank <= GetMaxHBlank(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHTotal()
{
	return HTotal >= GetMinHTotal(1) && HTotal <= GetMaxHTotal(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVActive()
{
	return VActive >= MinVActive[1] && VActive <= MaxVActive[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVFront()
{
	return VFront >= MinVFront[1] && VFront <= MaxVFront[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVSync()
{
	return VSync >= MinVSync[1] && VSync <= MaxVSync[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVBack()
{
	return VBack >= GetMinVBack(1) && VBack <= GetMaxVBack(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVBlank()
{
	return VBlank >= GetMinVBlank(1) && VBlank <= GetMaxVBlank(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVTotal()
{
	return VTotal >= GetMinVTotal(1) && VTotal <= GetMaxVTotal(1);
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedVRate()
{
	return VRate >= MinVRate[1] && VRate <= MaxVRate[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedHRate()
{
	return HRate >= MinHRate[1] && HRate <= MaxHRate[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedPClock()
{
	return PClock >= MinPClock[1] && PClock <= MaxPClock[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedActualVRate()
{
	return ActualVRate >= MinVRate[1] && ActualVRate <= MaxVRate[1];
}
//---------------------------------------------------------------------------
bool DetailedResolutionClass::IsSupportedActualHRate()
{
	return ActualHRate >= MinHRate[1] && ActualHRate <= MaxHRate[1];
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinHBack(int Type)
{
	return MinHBack[Type];
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxHBack(int Type)
{
	int MinMaxHFront = MinMax(HFront, MinHFront[Type], MaxHFront[Type]);
	int MinMaxHSync = MinMax(HSync, MinHSync[Type], MaxHSync[Type]);
	return Min(MaxHBack[Type], MaxHBlank[Type] - MinMaxHFront - MinMaxHSync);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinHBlank(int Type)
{
	int MinMaxHFront = MinMax(HFront, MinHFront[Type], MaxHFront[Type]);
	int MinMaxHSync = MinMax(HSync, MinHSync[Type], MaxHSync[Type]);
	return Max(MinHBlank[Type], MinMaxHFront + MinMaxHSync + MinHBack[Type]);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxHBlank(int Type)
{
	return MaxHBlank[Type];
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinHTotal(int Type)
{
	int MinMaxHActive = MinMax(HActive, MinHActive[Type], MaxHActive[Type]);
	int MinMaxHFront = MinMax(HFront, MinHFront[Type], MaxHFront[Type]);
	int MinMaxHSync = MinMax(HSync, MinHSync[Type], MaxHSync[Type]);
	return Max(MinHTotal[Type], MinMaxHActive + MinMaxHFront + MinMaxHSync + MinHBack[Type]);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxHTotal(int Type)
{
	if (HActive < MinHActive[Type] || HActive > MaxHActive[Type])
		return MaxHTotal[Type];

	int MinMaxHActive = MinMax(HActive, MinHActive[Type], MaxHActive[Type]);
	return Min(MaxHTotal[Type], MinMaxHActive + MaxHBlank[Type]);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinVBack(int Type)
{
	return MinVBack[Type];
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxVBack(int Type)
{
	int MinMaxVFront = MinMax(VFront, MinVFront[Type], MaxVFront[Type]);
	int MinMaxVSync = MinMax(VSync, MinVSync[Type], MaxVSync[Type]);
	return Min(MaxVBack[Type], MaxVBlank[Type] - MinMaxVFront - MinMaxVSync);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinVBlank(int Type)
{
	int MinMaxVFront = MinMax(VFront, MinVFront[Type], MaxVFront[Type]);
	int MinMaxVSync = MinMax(VSync, MinVSync[Type], MaxVSync[Type]);
	return Max(MinVBlank[Type], MinMaxVFront + MinMaxVSync + MinVBack[Type]);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxVBlank(int Type)
{
	return MaxVBlank[Type];
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMinVTotal(int Type)
{
	int MinMaxVActive = MinMax(VActive, MinVActive[Type], MaxVActive[Type]);
	int MinMaxVFront = MinMax(VFront, MinVFront[Type], MaxVFront[Type]);
	int MinMaxVSync = MinMax(VSync, MinVSync[Type], MaxVSync[Type]);
	return Max(MinVTotal[Type], MinMaxVActive + MinMaxVFront + MinMaxVSync + MinVBack[Type]);
}
//---------------------------------------------------------------------------
int DetailedResolutionClass::GetMaxVTotal(int Type)
{
	if (VActive < MinVActive[Type] || VActive > MaxVActive[Type])
		return MaxVTotal[Type];

	int MinMaxVActive = MinMax(VActive, MinVActive[Type], MaxVActive[Type]);
	return Min(MaxVTotal[Type], MinMaxVActive + MaxVBlank[Type]);
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::Min(long long Value1, long long Value2)
{
	if (Value1 <= Value2)
		return Value1;

	return Value2;
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::Max(long long Value1, long long Value2)
{
	if (Value1 >= Value2)
		return Value1;

	return Value2;
}
//---------------------------------------------------------------------------
long long DetailedResolutionClass::MinMax(long long Value, long long Min, long long Max)
{
	if (Value == BLANK)
		return Min;

	if (Value <= Min)
		return Min;

	if (Value >= Max)
		return Max;

	return Value;
}
//---------------------------------------------------------------------------
