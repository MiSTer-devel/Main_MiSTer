// hdd.h

#ifndef __MINIMIG_HDD_H__
#define __MINIMIG_HDD_H__

// Structure definitions for RDB emulation.
// For hardfiles that have no RDB information, we'll just create a single-partition RDB and Part block
// on blocks 0 and 1.  All other blocks within the first cylinder will be translated into the hardfile

#define RDB_MAGIC 0x4B534452 // "RDSK"

struct RigidDiskBlock {
	unsigned long rdb_ID;              // "RDSK"
	unsigned long rdb_Summedlongs;     // 0x40
	long          rdb_ChkSum;          // Sum to zero
	unsigned long rdb_HostID;          // 0x07
	unsigned long rdb_BlockBytes;      // 0x200
	unsigned long rdb_Flags;           // 0x12 (Disk ID valid, no LUNs after this one)
	unsigned long rdb_BadBlockList;    // -1 since we don't provide one
	unsigned long rdb_PartitionList;   // 1
	unsigned long rdb_FileSysHeaderList; // -1
	unsigned long rdb_DriveInit;       // -1
	unsigned long rdb_Reserved1[6];    // 0xffffffff
	unsigned long rdb_Cylinders;
	unsigned long rdb_Sectors;
	unsigned long rdb_Heads;
	unsigned long rdb_Interleave;      // 1
	unsigned long rdb_Park;            // =Cylinder count
	unsigned long rdb_Reserved2[3];
	unsigned long rdb_WritePreComp;    // High cylinder ?
	unsigned long rdb_ReducedWrite;    // High cylinder ?
	unsigned long rdb_StepRate;        // 3 ?
	unsigned long rdb_Reserved3[5];
	unsigned long rdb_RDBBlocksLo;     // block zero
	unsigned long rdb_RDBBlocksHi;     // block one
	unsigned long rdb_LoCylinder;      // 1
	unsigned long rdb_HiCylinder;      // From the hardfile: cylinder count -1
	unsigned long rdb_CylBlocks;       // From the hardfile: heads * sectors
	unsigned long rdb_AutoParkSeconds; // zero
	unsigned long rdb_HighRDSKBlock;   // 1
	unsigned long rdb_Reserved4;
	char          rdb_DiskVendor[8];   // "Don't"
	char          rdb_DiskProduct[16]; // " repartition!"
	char          rdb_DiskRevision[4];
	char          rdb_ControllerVendor[8];
	char          rdb_ControllerProduct[16];
	char          rdb_ControllerRevision[4];
	unsigned long rdb_Reserved5[10];
} __attribute__((packed));

struct DosEnvec {
	unsigned long de_TableSize;	     // Size of Environment vector - 0x10
	unsigned long de_SizeBlock;	     // in longwords - 0x80
	unsigned long de_SecOrg;	     // 0
	unsigned long de_Surfaces;		 // Heads?
	unsigned long de_SectorPerBlock; // 1
	unsigned long de_BlocksPerTrack;
	unsigned long de_Reserved;	     // 2 ?
	unsigned long de_PreAlloc;	     // 0
	unsigned long de_Interleave;     // 0
	unsigned long de_LowCyl;
	unsigned long de_HighCyl;
	unsigned long de_NumBuffers;     // 30
	unsigned long de_BufMemType;     // 0 - any available
	unsigned long de_MaxTransfer;    // 0x00ffffff
	unsigned long de_Mask;           // 0x7ffffffe
	long          de_BootPri;	     // 0
	unsigned long de_DosType;	     // 0x444f5301 or 3
							         // Extra fields
	unsigned long de_Baud;
	unsigned long de_Control;
	unsigned long de_BootBlocks;
} __attribute__((packed));

struct PartitionBlock {
	unsigned long pb_ID;             // "PART"
	unsigned long pb_Summedlongs;	 // 0x40
	long          pb_ChkSum;		 // Sum to zero
	unsigned long pb_HostID;		 // 0x07
	unsigned long pb_Next;           // -1
	unsigned long pb_Flags;          // 1 - Bootable
	unsigned long pb_Reserved1[2];   // 0
	unsigned long pb_DevFlags;       // 0
	char          pb_DriveName[32];  // 0x03"DH0"
	unsigned long pb_Reserved2[15];
	DosEnvec      pb_Environment;
	unsigned long pb_EReserved[12];  // reserved for future environment vector
} __attribute__((packed));

#endif // __HDD_H__
