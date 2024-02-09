#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libcoro.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct my_context {
	char *name;
	char **filenames;
	int nfiles;
	int *file_ind;
	int **arrays;
	int *array;
	int *arr_sizes;
	struct timespec start_time;
	struct timespec end_time;
	long work_time;
	/** ADD HERE YOUR OWN MEMBERS, SUCH AS FILE NAME, WORK TIME, ... */
};

static struct my_context *
my_context_new(const char *name, char **filenames, int nfiles,
				int *file_ind, int **arrays, int *sizes)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->filenames = filenames;
	ctx->nfiles = nfiles;
	ctx->file_ind = file_ind;
	ctx->arrays = arrays;
	ctx->arr_sizes = sizes;
	ctx->work_time = 0;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}


void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int partition(int *arr, int low, int high) {
    int pivot = arr[high];  // Выбираем последний элемент в качестве опорного
    int i = (low - 1);  // Индекс меньшего элемента
    for (int j = low; j <= high - 1; j++) {
        if (arr[j] < pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

void quicksort(int *arr, int low, int high, struct my_context *ctx) {
    if (low < high) {
        int p = partition(arr, low, high);
        quicksort(arr, low, p - 1, ctx);
        quicksort(arr, p + 1, high, ctx);
		
        clock_gettime(CLOCK_MONOTONIC, &ctx->end_time);

		ctx->work_time += (ctx->end_time.tv_sec - ctx->start_time.tv_sec) * 1000000 +
                    (ctx->end_time.tv_nsec - ctx->start_time.tv_nsec) / 1000;
		
		coro_yield();

		clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    }
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF INDIVIDUAL FILES HERE. */

	struct coro *this = coro_this();
	struct my_context *ctx = context;
	
	clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);

	while (*ctx->file_ind < ctx->nfiles) {
		char *filename = ctx->filenames[*ctx->file_ind];
		FILE *f = fopen(filename, "r");
		if (!f) {
			fprintf(stderr, "Error while opening file\r\n");
			my_context_delete(ctx);
			return 1;
		}
		int cur_size = 0;
		int max_size = 100;
		ctx->array = realloc(ctx->array, max_size * sizeof(int));
		
		while (fscanf(f, "%d", ctx->array + cur_size) == 1) {
			cur_size++;
			if (cur_size == max_size) {
				max_size *= 2;
				ctx->array = realloc(ctx->array, max_size * sizeof(int));
			}
		}

		max_size = cur_size;
		ctx->array = realloc(ctx->array, max_size * sizeof(int));

		ctx->arrays[*ctx->file_ind] = ctx->array;
		ctx->arr_sizes[*ctx->file_ind] = cur_size;

		quicksort(ctx->array, 0, cur_size - 1, ctx);

		fclose(f);

		(*ctx->file_ind)++;
	}

	printf("%s switch count: %lld\n",
	 	ctx->name,
	    coro_switch_count(this)
	);
	printf("%s worked %ld us\n", ctx->name, ctx->work_time);
	
	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}

void mergeArrays(int **arr1, int size1, int *arr2, int size2) {
    int *temp = calloc(size1, sizeof(int));

    for (int i = 0; i < size1; i++) {
        temp[i] = (*arr1)[i];
    }

	*arr1 = realloc(*arr1, sizeof(int) * (size1 + size2));
    int i = 0, j = 0, k = 0;

    while (i < size1 && j < size2) {
        if (temp[i] <= arr2[j]) {
            (*arr1)[k] = temp[i];
            i++;
        } else {
            (*arr1)[k] = arr2[j];
            j++;
        }
        k++;
    }

    while (i < size1) {
        (*arr1)[k] = temp[i];
        i++;
        k++;
    }
    
    while (j < size2) {
        (*arr1)[k] = arr2[j];
        j++;
        k++;
    }

	free(temp);
}

int
main(int argc, char **argv)
{
	struct timespec st;
	clock_gettime(CLOCK_MONOTONIC, &st);

	coro_sched_init();

	int nfiles = argc - 1;

	int *arrays[nfiles];
	int sizes[nfiles];
	int file_ind = 0;

	/* Start several coroutines. */
	for (int i = 0; i < nfiles; ++i) {
		char name[16];
		sprintf(name, "coro_%d", i);

		coro_new(coroutine_func_f, my_context_new(name, argv + 1, nfiles,
													&file_ind, arrays, sizes));
	}
	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("finished with status: %d\n\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */
	
	int result_size = 0;
	for (int i = 0; i < nfiles; i++)
	{
		result_size += sizes[i];
	}

	int *result = calloc(result_size, sizeof(int));
	result_size = 0;
 
 	for (int i = 0; i < nfiles; i++) {
		mergeArrays(&result, result_size, arrays[i], sizes[i]);
		result_size += sizes[i];
	}
	FILE *f = fopen("result.txt", "w");

	if (!f) {
		fprintf(stderr, "Error while opening file");
		return 1;
	}

	for (int i = 0; i < result_size; i++)
	{
		fprintf(f, "%d ", result[i]);
	}

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("total time: %ld us\n", 
			(end.tv_sec - st.tv_sec) * 1000000 + (end.tv_nsec - st.tv_nsec) / 1000);
	
	for (int i = 0; i < nfiles; i++)
	{
		free(arrays[i]);
	}
	
	free(result);

	fclose(f);

	return 0;
}
