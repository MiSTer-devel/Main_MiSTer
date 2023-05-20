#include "str_util.h"

#include <string.h>

int str_tokenize(char *s, const char *delim, char **tokens, int max_tokens)
{
	char *save_ptr = nullptr;
	int count = 0;

	char *token = strtok_r(s, delim, &save_ptr);

	while (token && count < max_tokens)
	{
		tokens[count] = token;
		count++;
		token = strtok_r(nullptr, delim, &save_ptr);
	}

	return count;
}

char *strncpyz(char *dest, size_t dest_size, const char *src, size_t num)
{
	size_t n = num >= dest_size ? dest_size - 1 : num;
	strncpy(dest, src, n);
	dest[n] = '\0';

	return dest;
}

char *strcpyz(char *dest, size_t dest_size, const char *src)
{
	return strncpyz(dest, dest_size, src, dest_size - 1);
}

unsigned int str_hash(const char *s, unsigned int initial)
{
	unsigned int hash = initial;
	int c;

	while((c = *s++))
	{
		hash = ((hash << 5) + hash) + c;
	}

	return hash;
}