#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>

#include "log.h"
#include "file.h"

char *
plxr_alloc_file(const char *path, off_t *size)
{
	struct stat info;
	char *buffer;
	int fd;

	if ((fd = open(path, O_RDONLY)) == -1)
	{
		ERROR("open");
		return NULL;
	}

	if ((fstat(fd, &info) == -1))
	{
		close(fd); /* clean up file descriptor */
		ERROR("fstat");
		return NULL;
	}

	if ((buffer = malloc(info.st_size + 1)) == NULL)
	{
		close(fd);
		ERROR("malloc");
		return NULL;
	}

	if ((read(fd, buffer, info.st_size) == -1))
	{
		close(fd);
		free(buffer); /* clean up this buffer since we're not returning it */
		ERROR("read");
		return NULL;
	}

	buffer[info.st_size] = '\0'; /* null terminator */
	*size = info.st_size;
	close(fd);
	return buffer;
}

/* ftw(3) forces you to use global variables to share state with the callback
 * so the first thing we do is implement a wrapper with a shared void* pointer.
 */
void *plxr_nftw_ptr = NULL;
int (*plxr_nftw_cb)(const char *, const struct stat *, int, struct FTW *, void *) = NULL;

int
plxr_nftw_cb_wrapper(const char *_path, const struct stat *_stat, int _flag, struct FTW *_ftw)
{
	return plxr_nftw_cb(_path, _stat, _flag, _ftw, plxr_nftw_ptr);
}

int
plxr_nftw(
	const char *path,
	int (*cb)(const char *, const struct stat *stat_ptr, int flag, struct FTW *, void *ptr), /* TODO TYPE */
	int depth,
	int flags,
	void *ptr)
{
	plxr_nftw_cb = cb;
	plxr_nftw_ptr = ptr;
	return nftw(path, &plxr_nftw_cb_wrapper, depth, flags);
}


/* Now we define a context struct to pass into our callback.
 */

struct plxr_stat_ctx {
	char *path; /* passed in to ntfw cb */
	plxr_filetype_t type; /* will be overwritten if a file is found */
};

int
plxr_stat_cb(const char *path, const struct stat *stat_ptr, int flag, struct FTW *ftw_ptr, void *ptr)
{
	struct plxr_stat_ctx *ctx = (struct plxr_stat_ctx *)ptr;

	/* We only care about regular files and directories for now. */
	if ((flag != FTW_F)
		&& (flag != FTW_D)) {
		return 0; /* nftw continue */
	}

	 /* Cut the leading "./" */
	if (path[0] == '.'
		&& path[1] == '/') {
		path += 2;
	}

	if (strcmp(path, ctx->path) == 0) {
		/* We have a match! */
		if (flag == FTW_F)
			ctx->type = PLX_FILE_REG;
		if (flag == FTW_D)
			ctx->type = PLX_FILE_DIR;
		return 1; /* nftw break */
	}

	return 0; /* nftw continue */
}

/* current directory stat:
 * finds a file in the current directory.
 */
plxr_filetype_t
plxr_stat(char *path)
{
	struct plxr_stat_ctx ctx = {
		.path = path,
		.type = PLX_FILE_NOT_FOUND
	};

	if (plxr_nftw(".", &plxr_stat_cb, 1, FTW_PHYS | FTW_MOUNT,  &ctx) == -1)
		return PLX_FILE_ERR;

	return ctx.type;
}
