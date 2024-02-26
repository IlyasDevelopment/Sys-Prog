#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

#define MAX_ARGS 50
#define MAX_COMMANDS 10
#define MAX_TOKEN_SIZE 256

void execute_command(char *args[], int input_fd, int output_fd) {
    pid_t pid = fork();

    if (pid == 0) {
        if (input_fd != 0) {
            dup2(input_fd, 0);
            close(input_fd);
        }

        if (output_fd != 1) {
            dup2(output_fd, 1);
            close(output_fd);
        }

        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        wait(NULL);
    } else {
        perror("fork");
        exit(EXIT_FAILURE);
    }
}

int main() {
    char input[MAX_TOKEN_SIZE];
    char *commands[MAX_COMMANDS][MAX_ARGS];
    char *token;

    while (1) {
        printf("> ");
        fgets(input, sizeof(input), stdin);

        input[strcspn(input, "\n")] = '\0';

        int command_count = 0;

        token = strtok(input, "|");
        while (token != NULL) {
            char *args[MAX_ARGS];
            int arg_count = 0;


            char *arg_token = strtok(token, " ");
            while (arg_token != NULL) {
                args[arg_count++] = arg_token;
                arg_token = strtok(NULL, " ");
            }

            args[arg_count] = NULL;

            commands[command_count][0] = strdup(args[0]);
            commands[command_count][1] = NULL;

            for (int i = 1; i < arg_count; ++i) {
                commands[command_count][i] = strdup(args[i]);
                commands[command_count][i + 1] = NULL;
            }

            ++command_count;

            token = strtok(NULL, "|");
        }

        int input_fd = 0;

        for (int i = 0; i < command_count; ++i) {
            int output_fd;

            if (i == command_count - 1) {
                output_fd = 1;
            } else {
                int pipe_fd[2];
                pipe(pipe_fd);
                output_fd = pipe_fd[1];
                input_fd = pipe_fd[0];
            }

            execute_command(commands[i], input_fd, output_fd);

            if (input_fd != 0) {
                close(input_fd);
            }

            if (output_fd != 1) {
                close(output_fd);
            }
        }
    }

    return 0;
}