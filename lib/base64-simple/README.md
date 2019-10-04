![Version](https://img.shields.io/badge/Version-0.1.0-green.svg)

Base64 Simple
=============

The goal of this project is to provide a dynamically linked library that
makes it dead-simple to encode and decode base64 strings from within your
program code. See the Usage section for more info.

Table of Contents
-----------------

1. [Download and Install](#download-and-install)
2. [Usage](#usage)
3. [License](#license)
4. [Tips](#tips)

Download and Install
--------------------

In order to download and build this library, you will need to have `git`,
`gcc`, and `make` installed. Install them from your package manager if not
already installed.

```
$ which make
/usr/bin/make

$ which gcc
/usr/bin/gcc

$ which git
/usr/bin/git
```

Download and Install:

```
$ git clone https://github.com/bartobri/base64-simple.git
$ cd base64-simple
$ make
$ sudo make install
```

Uninstall:

```
$ sudo make uninstall
```

Usage
-----

**Synopsys**

myprogram.c
```
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "base64simple.h"

int main(void) {
	char *decoded, *encoded;
	size_t i, size, r_size;

	// Encoding

	decoded = "This is a decoded string.";
	size = strlen(decoded);
	encoded = base64simple_encode(decoded, size);
	if (encoded == NULL) {
		printf("Insufficient Memory!\n");
	} else {
		printf("Encoded: %s\n", encoded);
	}

	// Decoding

	size = strlen(encoded);
	decoded = base64simple_decode(encoded, size, &r_size);
	if (decoded == NULL) {
		printf("Improperly Encoded String or Insufficient Memory!\n");
	} else {
		for (i = 0; i < r_size; ++i) {
			// Do something with decoded[i] here
		}
	}

	// Freeing

	free(encoded);
	free(decoded);

	return 0;
}
```

**Compiling**

You must tell the compiler to include the base64simple library.

```
gcc myprogram.c -lbase64simple
```

**Functions**

`char *base64simple_encode(unsigned char *s, size_t n)`

The base64simple_encode() function encodes the first **n** bytes of the
character string pointed to by **s**. It returns a pointer to a null-terminated
string containing the encoded result. A null poointer is returned if there
is insufficient memory for the encoded string.

`unsigned char *base64simple_decode(char *s, size_t n, size_t *x)`

The base64simple_decode() function decodes the first **n** bytes of the
character string pointed to by **s** (unless it encounters the base64
end-of-stream signature before that). It returns a pointer to an unsigned
character string containing the decoded result, and it assigns the length
of the decoded string to **x**. Note that this string is *NOT* null
terminated, so you must use the value of **x** to examine the contents
without encountering an out-of-bounds error. A null pointer is returned if **s**
is not properly encoded, or if there is insufficient memory for the decoded
string.

License
-------

This program is free software; you can redistribute it and/or modify it under the terms of the the
MIT License (MIT). See [LICENSE](LICENSE) for more details.

Tips
----

[Tips are always appreciated!](https://github.com/bartobri/tips)
