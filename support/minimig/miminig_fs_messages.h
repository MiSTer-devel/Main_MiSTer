#define ACTION_NIL            0
#define ACTION_GET_BLOCK      2
#define ACTION_SET_MAP        4
#define ACTION_DIE            5
#define ACTION_EVENT          6
#define ACTION_CURRENT_VOLUME 7
#define ACTION_LOCATE_OBJECT  8
#define ACTION_RENAME_DISK    9
#define ACTION_WRITE          'W'
#define ACTION_READ           'R'
#define ACTION_FREE_LOCK      15
#define ACTION_DELETE_OBJECT  16
#define ACTION_RENAME_OBJECT  17
#define ACTION_MORE_CACHE     18
#define ACTION_COPY_LOCK      19
#define ACTION_WAIT_CHAR      20
#define ACTION_SET_PROTECT    21
#define ACTION_CREATE_DIR     22
#define ACTION_EXAMINE_OBJECT 23
#define ACTION_EXAMINE_NEXT   24
#define ACTION_DISK_INFO      25
#define ACTION_INFO           26
#define ACTION_FLUSH          27
#define ACTION_SET_COMMENT    28
#define ACTION_PARENT         29
#define ACTION_TIMER          30
#define ACTION_INHIBIT        31
#define ACTION_DISK_TYPE      32
#define ACTION_DISK_CHANGE    33
#define ACTION_SET_DATE       34
#define ACTION_SAME_LOCK      40
#define ACTION_SCREEN_MODE    994
#define ACTION_READ_RETURN    1001
#define ACTION_WRITE_RETURN   1002
#define ACTION_FINDUPDATE     1004
#define ACTION_FINDINPUT      1005
#define ACTION_FINDOUTPUT     1006
#define ACTION_END            1007
#define ACTION_SEEK           1008
#define ACTION_TRUNCATE       1022
#define ACTION_WRITE_PROTECT  1023
#define ACTION_EXAMINE_FH     1034

#define ERROR_NO_FREE_STORE            103
#define ERROR_TASK_TABLE_FULL          105
#define ERROR_LINE_TOO_LONG            120
#define ERROR_FILE_NOT_OBJECT          121
#define ERROR_INVALID_RESIDENT_LIBRARY 122
#define ERROR_NO_DEFAULT_DIR           201
#define ERROR_OBJECT_IN_USE            202
#define ERROR_OBJECT_EXISTS            203
#define ERROR_DIR_NOT_FOUND            204
#define ERROR_OBJECT_NOT_FOUND         205
#define ERROR_BAD_STREAM_NAME          206
#define ERROR_OBJECT_TOO_LARGE         207
#define ERROR_ACTION_NOT_KNOWN         209
#define ERROR_INVALID_COMPONENT_NAME   210
#define ERROR_INVALID_LOCK             211
#define ERROR_OBJECT_WRONG_TYPE        212
#define ERROR_DISK_NOT_VALIDATED       213
#define ERROR_DISK_WRITE_PROTECTED     214
#define ERROR_RENAME_ACROSS_DEVICES    215
#define ERROR_DIRECTORY_NOT_EMPTY      216
#define ERROR_TOO_MANY_LEVELS          217
#define ERROR_DEVICE_NOT_MOUNTED       218
#define ERROR_SEEK_ERROR               219
#define ERROR_COMMENT_TOO_BIG          220
#define ERROR_DISK_FULL                221
#define ERROR_DELETE_PROTECTED         222
#define ERROR_WRITE_PROTECTED          223
#define ERROR_READ_PROTECTED           224
#define ERROR_NOT_A_DOS_DISK           225
#define ERROR_NO_DISK                  226
#define ERROR_NO_MORE_ENTRIES          232

#define SHARED_LOCK    -2
#define EXCLUSIVE_LOCK -1

#define LOCK_DIFFERENT   -1
#define LOCK_SAME        0
#define LOCK_SAME_VOLUME 1

#define MODE_OLDFILE    1005
#define MODE_NEWFILE    1006
#define MODE_READWRITE  1004

#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT    0
#define OFFSET_END        1

#define ST_ROOT      1
#define ST_USERDIR   2
#define ST_SOFTLINK  3
#define ST_LINKDIR   4
#define ST_FILE     -3
#define ST_LINKFILE -4
#define ST_PIPEFILE -5

#pragma pack(push, 1)

struct GenericRequestResponse
{
	long sz;
	union
	{
		struct
		{
			long type;
			long key;
		};

		struct
		{
			long success;
			long error_code;
		};
	};
};

struct LocateObjectRequest
{
	long sz;
	long type;
	long key;
	long mode;
	char name[1];
};

struct LocateObjectResponse
{
	long sz;
	long success;
	long error_code;
	long key;
};

struct FreeLockRequest
{
	long sz;
	long type;
	long key;
};

struct FreeLockResponse
{
	long sz;
	long success;
	long error_code;
};

struct CopyDirRequest
{
	long sz;
	long type;
	long key;
};

struct CopyDirResponse
{
	long sz;
	long success;
	long error_code;
	long key;
};

struct ParentRequest
{
	long sz;
	long type;
	long key;
};

struct ParentResponse
{
	long sz;
	long success;
	long error_code;
	long key;
};

struct ExamineObjectRequest
{
	long sz;
	long type;
	long key;
	long disk_key;
};

struct ExamineObjectResponse
{
	long sz;
	long success;
	long error_code;

	long disk_key;
	long entry_type;
	int size;
	int protection;
	int date[3];
	char file_name[1];
};

struct FindXxxRequest
{
	long sz;
	long type;
	long key;
	char name[1];
};

struct FindXxxResponse
{
	long sz;
	long success;
	long error_code;
	long arg1;
};

struct ReadRequest
{
	long sz;
	long type;
	long arg1;
	int address;
	int length;
};

struct ReadResponse
{
	long sz;
	long success;
	long error_code;
	int actual;
};

struct WriteRequest
{
	long sz;
	long type;
	long arg1;
	int address;
	int length;
};

struct WriteResponse
{
	long sz;
	long success;
	long error_code;
	int actual;
};

struct SeekRequest
{
	long sz;
	long type;
	long arg1;
	int new_pos;
	int mode;
};

struct SeekResponse
{
	long sz;
	long success;
	long error_code;
	int old_pos;
};

struct EndRequest
{
	long sz;
	long type;
	long arg1;
};

struct EndResponse
{
	long sz;
	long success;
	long error_code;
};

struct DeleteObjectRequest
{
	long sz;
	long type;
	long key;
	char name[1];
};

struct DeleteObjectResponse
{
	long sz;
	long success;
	long error_code;
};

struct RenameObjectRequest
{
	long sz;
	long type;
	long key;
	long target_dir;
	unsigned char name_len;
	unsigned char new_name_len;
	char name[1];
};

struct RenameObjectResponse
{
	long sz;
	long success;
	long error_code;
};

struct CreateDirRequest
{
	long sz;
	long type;
	long key;
	char name[1];
};

struct CreateDirResponse
{
	long sz;
	long success;
	long error_code;
	long key;
};

struct SetProtectRequest
{
	long sz;
	long type;
	long key;
	long mask;
	char name[1];
};

struct SetProtectResponse
{
	long sz;
	long success;
	long error_code;
};

struct SetCommentRequest
{
	long sz;
	long type;
	long key;
	unsigned char name_len;
	unsigned char comment_len;
};

struct SetCommentResponse
{
	long sz;
	long success;
	long error_code;
};

struct SameLockRequest
{
	long sz;
	long type;
	long key1;
	long key2;
};

struct SameLockResponse
{
	long sz;
	long success;
	long error_code;
};

struct ExamineFhRequest
{
	long sz;
	long type;
	long arg1;
};

struct ExamineFhResponse
{
	long sz;
	long success;
	long error_code;

	long disk_key;
	long entry_type;
	int size;
	int protection;
	int date[3];
	char file_name[1];
};

struct DiskInfoRequest
{
	long sz;
	long type;
	long key;
	long dummy1;
	long dummy2;
};

struct DiskInfoResponse
{
	long sz;
	long success;
	long error_code;
	unsigned long total;
	unsigned long used;
	long update;
};

#pragma pack(pop)
