#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

	/* PUT HERE OTHER MEMBERS */
	int open_mode;
	size_t pos;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
	/* IMPLEMENT THIS FUNCTION */
	int fd = -1;
	struct file * f = file_list;
	while (f) {
		if (f->name && strcmp(f->name, filename) == 0)
			break;
		f = f->next;
	}
	if (flags == UFS_CREATE) flags = UFS_CREATE | UFS_READ_WRITE;
	else if (flags == 0) flags = UFS_READ_WRITE;
	if (!f) {
		if (flags & UFS_CREATE) {
			f = (struct file *) malloc(sizeof(struct file));
			f->block_list = NULL;
			f->last_block = NULL;
			f->refs = 1;
			f->name = (char *) malloc(strlen(filename)+1);
			strcpy(f->name, filename);
			f->next = file_list;
			f->prev = NULL;
			if (file_list)
				file_list->prev = f;
			file_list  = f;
		} else {
			ufs_error_code = UFS_ERR_NO_FILE;
			return -1;
		}
	} else {
		f->refs++;
		if (flags & UFS_CREATE) {
			struct block * fb = f->block_list;
			while (fb) {
				f->block_list = fb->next;
				free(fb->memory);
				free(fb);
				fb = f->block_list;
			}
			f->last_block = NULL;
		}
	}
	if (file_descriptor_count == file_descriptor_capacity) {
		file_descriptor_capacity += 10;
		file_descriptors = (struct filedesc **) realloc(file_descriptors, file_descriptor_capacity*sizeof(struct filedesc *));
	}
	fd = file_descriptor_count++;
	file_descriptors[fd] = (struct filedesc *) malloc(sizeof(struct filedesc));
	file_descriptors[fd]->file = f;
	file_descriptors[fd]->open_mode = flags;
	file_descriptors[fd]->pos = 0;

	ufs_error_code = UFS_ERR_NO_ERR;
	return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	if ((file_descriptors[fd]->open_mode & UFS_WRITE_ONLY) == 0 &&
		(file_descriptors[fd]->open_mode & UFS_READ_WRITE) == 0) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}
	if (file_descriptors[fd]->pos + size > MAX_FILE_SIZE) {
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}
	if (size > 0) {
		size_t pos = file_descriptors[fd]->pos;
		struct block * pfb = NULL;
		struct block * fb = file_descriptors[fd]->file->block_list;
		size_t nb = pos / BLOCK_SIZE;
		while (nb--) {
			pfb = fb;
			fb = fb->next;
		}
		if (!fb) {
			file_descriptors[fd]->file->last_block = (struct block *) malloc(sizeof(struct block));
			file_descriptors[fd]->file->last_block->memory = (char *) malloc(BLOCK_SIZE);
			file_descriptors[fd]->file->last_block->occupied = 0;
			file_descriptors[fd]->file->last_block->next = NULL;
			file_descriptors[fd]->file->last_block->prev = pfb;
			fb = file_descriptors[fd]->file->last_block;
			if (!pfb) {
				file_descriptors[fd]->file->block_list = fb;
			} else
				pfb->next = fb;
		}
		int rest = pos % BLOCK_SIZE;
		size_t write_cur, write_next;
		char * memo = fb->memory;
		int i;
		if (rest + size > BLOCK_SIZE) {
			write_cur = BLOCK_SIZE - rest;
			write_next = rest + size - BLOCK_SIZE;
		} else {
			write_cur = size;
			write_next = 0;
		}
		for (i = 0; write_cur; i++, rest++, write_cur--) {
			memo[rest] = buf[i];
		}
		if (rest > fb->occupied)
			fb->occupied = rest;
		while (write_next) {
			if (!fb->next) {
				file_descriptors[fd]->file->last_block = (struct block *) malloc(sizeof(struct block));
				file_descriptors[fd]->file->last_block->memory = (char *) malloc(BLOCK_SIZE);
				file_descriptors[fd]->file->last_block->occupied = 0;
				file_descriptors[fd]->file->last_block->next = NULL;
				file_descriptors[fd]->file->last_block->prev = fb;
				fb->next = file_descriptors[fd]->file->last_block;
				fb = file_descriptors[fd]->file->last_block;
			} else
				fb = fb->next;
			memo = fb->memory;
			for (rest = 0; write_next && rest < BLOCK_SIZE; i++, rest++, write_next--) {
				memo[rest] = buf[i];
			}
			if (rest > fb->occupied)
				fb->occupied = rest;
		}
	}
	file_descriptors[fd]->pos += size;
	ufs_error_code = UFS_ERR_NO_ERR;
	return size;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	int i = 0;
	if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	if ((file_descriptors[fd]->open_mode & UFS_READ_ONLY) == 0 &&
		(file_descriptors[fd]->open_mode & UFS_READ_WRITE) == 0) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}
	if (size > 0) {
		struct block * _fb = file_descriptors[fd]->file->block_list;
		size_t cur_length = 0;
		while (_fb) {
			cur_length += _fb->occupied;
			_fb = _fb->next;
		}
		size_t pos = file_descriptors[fd]->pos;
		if (pos + size > cur_length) {
			size = cur_length - pos;
		}
		if (size > 0) {
			struct block * fb = file_descriptors[fd]->file->block_list;
			size_t nb = pos / BLOCK_SIZE;
			size_t ns = pos % BLOCK_SIZE;
			while (nb--) {
				fb = fb->next;
			}
			size_t read_cur, read_next;
			char * memo = fb->memory;
			if (ns + size > BLOCK_SIZE) {
				read_cur = BLOCK_SIZE - ns;
				read_next = ns + size - BLOCK_SIZE;
			} else {
				read_cur = size;
				read_next = 0;
			}
			for (i = 0; read_cur; i++, ns++, read_cur--) {
				buf[i] = memo[ns];
			}
			while (read_next) {
				fb = fb->next;
				memo = fb->memory;
				for (ns = 0; read_next && ns < BLOCK_SIZE; i++, ns++, read_next--) {
					buf[i] = memo[ns];
				}
			}
		}
	}
	file_descriptors[fd]->pos += i;
	ufs_error_code = UFS_ERR_NO_ERR;
	return i;
}

void delete_file(struct file * f) {
	if (f) {
		free(f->name);
		if (f->prev)
			f->prev->next = f->next;
		else
			file_list = f->next;
		if (f->next)
			f->next->prev = f->prev;
		struct block * fb = f->block_list;
		while (fb) {
			f->block_list = fb->next;
			free(fb->memory);
			free(fb);
			fb = f->block_list;
		}
		free(f);
	}
}

int
ufs_close(int fd)
{
	/* IMPLEMENT THIS FUNCTION */
	if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	file_descriptors[fd]->file->refs--;
	if (file_descriptors[fd]->file->name == NULL && file_descriptors[fd]->file->refs == 0)
		delete_file(file_descriptors[fd]->file);
	free(file_descriptors[fd]);
	file_descriptors[fd] = NULL;

	ufs_error_code = UFS_ERR_NO_ERR;
	return 0;
}

int
ufs_delete(const char *filename)
{
	/* IMPLEMENT THIS FUNCTION */
	struct file * f = file_list;
	while (f) {
		if (f->name && strcmp(f->name, filename) == 0)
			break;
		f = f->next;
	}
	if (!f) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	if (f->refs > 0) {
		free(f->name);
		f->name = NULL;
	} else
		delete_file(f);
	ufs_error_code = UFS_ERR_NO_ERR;
	return 0;
}

void
ufs_destroy(void)
{
	while (file_list)
		delete_file(file_list);
	int i;
	for (i = 0; i < file_descriptor_count; i++)
		free(file_descriptors[i]);
	free(file_descriptors);
}

int
ufs_resize(int fd, size_t new_size) {
	if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	int mode = file_descriptors[fd]->open_mode;
	if ((mode & UFS_WRITE_ONLY) == 0 && (mode & UFS_READ_WRITE) == 0) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}
	if (new_size > MAX_FILE_SIZE) {
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}
	struct block * fb = file_descriptors[fd]->file->block_list;
	size_t cur_length = 0;
	while (fb) {
		cur_length += fb->occupied;
		fb = fb->next;
	}
	if (cur_length < new_size) {
		int now = cur_length / BLOCK_SIZE;
		int must = new_size / BLOCK_SIZE;
		if (file_descriptors[fd]->file->block_list == NULL) {
			file_descriptors[fd]->file->block_list =
				file_descriptors[fd]->file->last_block =
					(struct block *) malloc(sizeof(struct block));
			file_descriptors[fd]->file->block_list->memory = (char *) malloc(BLOCK_SIZE);
			file_descriptors[fd]->file->block_list->occupied = 0;
			file_descriptors[fd]->file->block_list->next = NULL;
			file_descriptors[fd]->file->block_list->prev = NULL;
		}
		size_t rest = new_size % BLOCK_SIZE;
		file_descriptors[fd]->file->last_block->occupied = BLOCK_SIZE;
		while (now < must) {
			fb = (struct block *) malloc(sizeof(struct block));
			fb->memory = (char *) malloc(BLOCK_SIZE);
			fb->occupied = BLOCK_SIZE;
			fb->prev = file_descriptors[fd]->file->last_block;
			file_descriptors[fd]->file->last_block->next = fb;
			file_descriptors[fd]->file->last_block = fb;
			fb->next = NULL;
			now++;
		}
		file_descriptors[fd]->file->last_block->occupied = rest;
	} else if (cur_length > new_size) {
		fb = file_descriptors[fd]->file->block_list;
		size_t cur_length = 0;
		while (fb && cur_length + fb->occupied < new_size) {
			cur_length += fb->occupied;
			fb = fb->next;
		}
		fb->occupied = new_size - cur_length;
		file_descriptors[fd]->file->last_block = fb;
		fb = fb->next;
		file_descriptors[fd]->file->last_block->next = NULL;
		while (fb) {
			free(fb->memory);
			struct block * next = fb->next;
			free(fb);
			fb = next;
		}
		int i;
		for (i = 0; i < file_descriptor_count; i++)
			if (file_descriptors[i] && file_descriptors[i]->file == file_descriptors[fd]->file
				&& file_descriptors[i]->pos > new_size) {
				file_descriptors[i]->pos = new_size;
			}
	}
	ufs_error_code = UFS_ERR_NO_ERR;
	return 0;
}
