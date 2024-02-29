#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

static int
execute_command_line(const struct command_line *line, int * exit_flag)
{
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */
	assert(line != NULL);
	pid_t tid = -1;

	if ((int)line->is_background)
		tid = fork();

	if (tid <= 0) {
		int old_stdout = dup(STDOUT_FILENO);
		int output = STDOUT_FILENO;
		if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
			output = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
			dup2(output, STDOUT_FILENO);
		} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
			output = open(line->out_file, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
			dup2(output, STDOUT_FILENO);
		}
		const struct expr *e = line->head;
		pid_t * ids = NULL;
		char *** args = NULL;
		int ret_code = 0;
		while (!(*exit_flag) && e != NULL) {
			int to_child[2] = { STDIN_FILENO };
			int n_processes = 0;
			int has_ret_code = 0;
			int i;
			while (!(*exit_flag) && e != NULL && e->type == EXPR_TYPE_COMMAND) {
				if (strcmp(e->cmd.exe, "cd") == 0) {
					if (e->cmd.arg_count != 1)
						ret_code = 1;
					else {
						ret_code = chdir(e->cmd.args[0]);
					}
				} else if (strcmp(e->cmd.exe, "exit") == 0) {
					if (e->next && e->next->type == EXPR_TYPE_PIPE) {
						// Perform nothing
						if (e->next && e->next->type == EXPR_TYPE_PIPE) e = e->next;
					} else {
						if (e->cmd.arg_count == 1)
							ret_code = atoi(e->cmd.args[0]);
						if (n_processes == 0)
							*exit_flag = 1;
						else
							has_ret_code = 1;
					}
				} else {
					args = (char ***) realloc(args, (n_processes+1)*sizeof(char **));
					args[n_processes] = (char **) malloc((e->cmd.arg_count+3)*sizeof(char *));
					args[n_processes][0] = e->cmd.exe;
					args[n_processes][1] = e->cmd.exe;

					for (uint32_t i = 0; i < e->cmd.arg_count; ++i) {
						args[n_processes][i+2] = e->cmd.args[i];
					}
					args[n_processes][e->cmd.arg_count+2] = NULL;

					n_processes++;
					ids = (pid_t *) realloc(ids, n_processes*sizeof(pid_t));
					pid_t id = 0;
					if (e->next && e->next->type == EXPR_TYPE_PIPE) {
						int old_in = to_child[0];
						pipe(to_child);
						id = fork();
						if (id == 0) {
							if (old_in != STDIN_FILENO) {
								dup2(old_in, STDIN_FILENO);
								close(old_in);
							}
							dup2(to_child[1], STDOUT_FILENO);
							close(to_child[0]);
							close(to_child[1]);
							execvp(args[n_processes-1][0], &args[n_processes-1][1]);
						}
						if (old_in != STDIN_FILENO) {
							close(old_in);
						}
						close(to_child[1]);
						e = e->next;
					} else {
						id = fork();
						if (id == 0) {
							if (to_child[0] != STDIN_FILENO) {
								dup2(to_child[0], STDIN_FILENO);
								close(to_child[0]);
							}
							execvp(args[n_processes-1][0], &args[n_processes-1][1]);
						}
						if (to_child[0] != STDIN_FILENO) {
							close(to_child[0]);
						}
					}
					ids[n_processes-1] = id;
				}
				e = e->next;
			}
			for (i = 0; i < n_processes; i++) {
				int status = 0;
				pid_t id = wait(&status);
				if (!has_ret_code && id == ids[n_processes-1])
					ret_code = WEXITSTATUS(status);
			}
			for (i = 0; i < n_processes; i++) {
				free(args[i]);
			}
			if (!(*exit_flag)) {
				if (e != NULL && e->type == EXPR_TYPE_AND) {
					if (ret_code == 0)
						e = e->next;
					else {
						while (e != NULL && e->type != EXPR_TYPE_OR)
							e = e->next;
						if (e)
							e = e->next;
					}
				} else if (e != NULL && e->type == EXPR_TYPE_OR) {
						if (ret_code == 0) {
							while (e != NULL && e->type != EXPR_TYPE_AND)
								e = e->next;
							if (e)
								e = e->next;
						} else
							e = e->next;
				}
			}
		}
		free(ids);
		free(args);
		if (output != STDOUT_FILENO)
			close(output);
		dup2(old_stdout, STDOUT_FILENO);

		if (tid == 0)
			*exit_flag = 1;

		return ret_code;
	} else
		return 0;
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	int exit_flag = 0;
	int ret_code = 0;
	struct parser *p = parser_new();
	while (!exit_flag && (rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (!exit_flag) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			ret_code = execute_command_line(line, &exit_flag);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return ret_code;
}
