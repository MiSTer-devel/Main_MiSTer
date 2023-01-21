/*
* str_util.h
*
*/

#ifndef STR_UTIL_H
#define STR_UTIL_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

int str_tokenize(char *s, const char *delim, char **tokens, int max_tokens);

// String copy with guaranteed null termination
char *strncpyz(char *dest, size_t dest_size, const char *src, size_t num);
char *strcpyz(char *dest, size_t dest_size, const char *src);

template<size_t N>
char *strncpyz(char (&dest)[N], const char *src, size_t num)
{
	return strncpyz(dest, N, src, num);
}

template<size_t N>
char *strcpyz(char (&dest)[N], const char *src)
{
	return strcpyz(dest, N, src);
}

template<size_t N>
size_t sprintfz(char (&dest)[N], const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	size_t r = vsnprintf(dest, N, fmt, args);
	va_end(args);
	return r;
}

#endif // STR_UTIL_H