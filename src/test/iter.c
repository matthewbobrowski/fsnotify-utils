/*
 * iter - fast iterate pre defined directory tree
 *
 * Copyright (C) 2016 CTERA Network by Amir Goldstein <amir73il@gmail.com>
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "iter.h"

int tree_depth;
int tree_width = 32;
int leaf_count = 32;
int node_count;
char *file_prefix = "f";
char *dir_prefix = "d";
int data_seed = 0;
int trace_depth = 0;
int start_id = 0;
int dir_id_log16 = 0;

void iter_usage()
{
	fprintf(stderr, "-w <dirtree width>    (default = 32)\n");
	fprintf(stderr, "-c <leaf files count> (default = 32)\n");
	fprintf(stderr, "-C <node files count> (default = 0)\n");
	fprintf(stderr, "-f <filename prefix>  (default = 'f')\n");
	fprintf(stderr, "-d <dirname prefix>   (default = 'd')\n");
	fprintf(stderr, "-v <trace depth>      (default = 0, implies -x)\n");
	fprintf(stderr, "-x <start global id>  (default = 0, id in hexa as printed by -v deepest trace prints)\n");
	fprintf(stderr, "-s <data type/seed>   (default = 0)\n");
	fprintf(stderr, "data type/seed may be 0 (default) for fallocate, < 0 for sparse file and > 0 for seed of random data\n");
}

int iter_parseopt(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "c:C:w:s:f:d:v:x:")) != -1) {
		switch (c) {
			case 'c':
				leaf_count = atoi(optarg);
				break;
			case 'C':
				node_count = atoi(optarg);
				break;
			case 'w':
				tree_width = atoi(optarg);
				break;
			case 's':
				data_seed = atoi(optarg);
				break;
			case 'f':
				file_prefix = optarg;
				break;
			case 'd':
				dir_prefix = optarg;
				break;
			case 'v':
				trace_depth = atoi(optarg);
				/* Implies -x so created dir name is a progress indicator */
				dir_id_log16 = 1;
				break;
			case 'x':
				start_id = strtoul(optarg, NULL, 16);
				dir_id_log16 = 1;
				break;
			default:
				fprintf(stderr, "illegal option '%s'\n", argv[optind]);
			case 'h':
				return -1;
		}
	}

	return 0;
}

static int create_name(char *buf, int len, int is_dir, int parent, int i)
{
	const char *prefix = is_dir ? dir_prefix : file_prefix;
	int id;

	if (dir_id_log16) {
		id = parent + i;
		snprintf(buf, len, "%s%x", prefix, id);
	} else {
		id = i;
		snprintf(buf, len, "%s%d", prefix, i);
	}

	return id;
}

static int iter_names(iter_op op, int depth, int parent)
{
	int ret;
	int i, count = depth ? tree_width : leaf_count;
	char name[NAME_MAX+1];

	name[NAME_MAX] = 0;
iter_files:
	for (i = 0; i < count; i++) {
		create_name(name, NAME_MAX, depth, parent, i);
		ret = op(name, depth);
		if (ret && errno != EEXIST && errno != ENOENT) {
			perror(name);
			return ret;
		}
	}

	if (depth && node_count) {
		depth = 0;
		parent += count;
		count = node_count;
		goto iter_files;
	}

	return 0;
}

#ifndef O_PATH
#define O_PATH 010000000
#endif

static int trace_begin(char *name, int depth, int id)
{
	int tabs = tree_depth - depth;
	int ret = id;

	if (id >= 0 && (tabs >= trace_depth))
		return 0;
	/* Skip only trace leaf id's */
	if (!start_id || (tabs < trace_depth - 1))
		ret = 0;
	else
		ret = (id < start_id) ? 1 : 0;
	while (tabs--)
		putchar('\t');
	printf("%s%s\n", name, ret < 0 ? " ABORTED!" :
			(ret ? " SKIPPED" : "..."));
	return ret;
}

static void trace_end(char *name, int depth, int ret)
{
	if (ret < 0) trace_begin(name, depth, ret);
}

int iter_dirs(iter_op op, int depth, int parent)
{
	int ret = 0;
	int fd = open(".", O_PATH);
	int i, count = tree_width;
	char name[NAME_MAX+1];
	int id;

	name[NAME_MAX] = 0;

	if (fd < 0) {
		perror("open(.)");
		return fd;
	}

	if (depth >= 0) // BFS
		ret = iter_names(op, depth, parent);

	if (ret || depth == 0)
		goto out;

	for (i = 0; i < count; i++) {
		id = create_name(name, NAME_MAX, 1, parent, i);
		if (ret = chdir(name) && errno != ENOENT) {
			perror("chdir");
			goto out;
		}

		ret = trace_begin(name, depth, id);
		if (!ret && depth > 0) // BFS
			ret = iter_dirs(op, depth - 1, id << (dir_id_log16*4));
		if (!ret && depth < 0) // DFS
			ret = iter_dirs(op, depth + 1, id << (dir_id_log16*4));
		trace_end(name, depth, ret);
		if (ret < 0)
			goto out;

		if (ret = fchdir(fd)) {
			perror("fchdir");
			goto out;
		}
	}
	if (depth < 0) // DFS
		ret = iter_names(op, depth, parent);

out:
	if (ret)
		fprintf(stderr, "name=%s depth=%d ret=%d\n", name, depth, ret);
	close(fd);
	return ret;
}


static unsigned int log16(unsigned int x)
{
	unsigned int ans = 0;

	while (x>>=4) ans++;
	return ans;
}

#define max(x, y) ((x) > (y) ? (x) : (y))

int iter_tree(iter_op op, int depth)
{
	// Calc number of hexa digits per dir level
	if (dir_id_log16)
		dir_id_log16 = log16(max(tree_width + node_count, leaf_count) - 1) + 1;

	return iter_dirs(op, depth, 1);
}
