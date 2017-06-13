#define ERROR_NONE 0
#define ERROR_FILE_NOT_FOUND 1
#define ERROR_INVALID_DATA 2
#define ERROR_UPDATE_FAILED 3

extern unsigned char Error;

void ErrorMessage(const char *message, unsigned char code);
void FatalError(unsigned long error);
