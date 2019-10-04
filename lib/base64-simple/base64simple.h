// Copyright (c) 2017 Brian Barto
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the MIT License. See LICENSE for more details.

#ifndef BASE64SIMPLE_H
#define BASE64SIMPLE_H 1

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Function Prototypes
 */
char *base64simple_encode(unsigned char *, size_t);
unsigned char *base64simple_decode(char *, size_t, size_t *);

#ifdef __cplusplus
}
#endif

#endif
