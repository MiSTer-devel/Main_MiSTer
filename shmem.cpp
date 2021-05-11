#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "shmem.h"

static int memfd = -1;

void *shmem_map(uint32_t address, uint32_t size)
{
	if (memfd < 0)
	{
		memfd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
		if (memfd == -1)
		{
			printf("Error: Unable to open /dev/mem!\n");
			return 0;
		}
	}

	void *res = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, address);
	if (res == (void *)-1)
	{
		printf("Error: Unable to mmap (0x%X, %d)!\n", address, size);
		return 0;
	}

	return res;
}

int shmem_unmap(void* map, uint32_t size)
{
	if (munmap(map, size) < 0)
	{
		printf("Error: Unable to unmap(0x%X, %d)!\n", (uint32_t)map, size);
		return 0;
	}

	return 1;
}

int shmem_put(uint32_t address, uint32_t size, void *buf)
{
	void *shmem = shmem_map(address, size);
	if (shmem)
	{
		memcpy(shmem, buf, size);
		shmem_unmap(shmem, size);
	}

	return shmem != 0;
}

int shmem_get(uint32_t address, uint32_t size, void *buf)
{
	void *shmem = shmem_map(address, size);
	if (shmem)
	{
		memcpy(buf, shmem, size);
		shmem_unmap(shmem, size);
	}

	return shmem != 0;
}
