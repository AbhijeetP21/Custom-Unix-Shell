/*
 * sh.c - A simple Unix shell with:
 *   - Execution of external commands (via fork/execvp)
 *   - I/O redirection (< and >)
 *   - Pipelines (commands separated by |)
 *   - Multiple commands per line separated by ';'
 *   - Built‑in "cd" command
 *   - Background execution (if command ends with &)
 *
 * Challenge features:
 *   - Command history (the built‑in "history" command prints all commands entered, excluding the "history" command itself)
 *   - Globbing: Wildcard expansion for arguments (using glob())
 *
 * Compile with:
 *      gcc -o utsh sh.c
 *
 * Then run:
 *      ./utsh
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <glob.h>

#define MAX_TOKENS 128

/* ------------------------ */
/* Global command history   */
/* ------------------------ */
static char **history = NULL;
static int history_count = 0;
static int history_capacity = 0;

void add_history(const char *line) {
    // Duplicate the line and remove a trailing newline if present.
    char *copy = strdup(line);
    if (!copy) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    size_t len = strlen(copy);
    if (len > 0 && copy[len - 1] == '\n') {
        copy[len - 1] = '\0';
    }
    if (history_capacity == 0) {
        history_capacity = 10;
        history = malloc(history_capacity * sizeof(char *));
        if (!history) {
            perror("malloc history");
            exit(EXIT_FAILURE);
        }
    }
    if (history_count >= history_capacity) {
        history_capacity *= 2;
        history = realloc(history, history_capacity * sizeof(char *));
        if (!history) {
            perror("realloc history");
            exit(EXIT_FAILURE);
        }
    }
    history[history_count++] = copy;
}

void print_history(void) {
    // Print history entries in the format: "1 pwd" (number, a space, then command)
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, history[i]);
    }
}

/* ------------------------ */
/* Command structure        */
/* ------------------------ */
typedef struct {
    char **args;      /* NULL-terminated array of arguments */
    char *infile;     /* Input redirection file (if any) */
    char *outfile;    /* Output redirection file (if any) */
    int background;   /* Nonzero if command is to run in the background */
} command_t;

/* Function prototypes */
char *read_line(void);
char **split_line(char *line, const char *delim);
char **expand_globs(char **args);
command_t *parse_command(char *cmd_str);
void free_command(command_t *cmd);
int execute_command(command_t *cmd);
int execute_pipeline(command_t **cmds, int num_cmds);

/* ------------------------ */
/* Read a line from input   */
/* ------------------------ */
char *read_line(void) {
    char *line = NULL;
    size_t bufsize = 0;
    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {  // EOF encountered
            free(line);
            return NULL;
        } else {
            perror("getline");
            free(line);
            return NULL;
        }
    }
    return line;
}

/* ------------------------ */
/* Split a string by delim  */
/* ------------------------ */
char **split_line(char *line, const char *delim) {
    int bufsize = MAX_TOKENS;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    if (!tokens) {
        fprintf(stderr, "Allocation error in split_line\n");
        exit(EXIT_FAILURE);
    }
    char *token = strtok(line, delim);
    while (token != NULL) {
        tokens[position++] = token;
        if (position >= bufsize) {
            bufsize += MAX_TOKENS;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "Allocation error in split_line\n");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, delim);
    }
    tokens[position] = NULL;
    return tokens;
}

/* ------------------------ */
/* Globbing expansion       */
/* ------------------------ */
/* For each argument (except index 0, the command name),
   if a wildcard character (*, ? or [) is found, use glob()
   to expand it into matching filenames. */
char **expand_globs(char **args) {
    int new_capacity = 16;
    int new_count = 0;
    char **new_args = malloc(new_capacity * sizeof(char *));
    if (!new_args) {
        perror("malloc expand_globs");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; args[i] != NULL; i++) {
        if (i == 0) {
            /* Do not expand the command name */
            new_args[new_count++] = strdup(args[i]);
        } else {
            if (strpbrk(args[i], "*?[") != NULL) {
                glob_t g;
                int flags = 0;
                int ret = glob(args[i], flags, NULL, &g);
                if (ret != 0) {
                    /* If glob fails or no match, use the original argument */
                    new_args[new_count++] = strdup(args[i]);
                } else {
                    for (size_t j = 0; j < g.gl_pathc; j++) {
                        if (new_count >= new_capacity - 1) {
                            new_capacity *= 2;
                            new_args = realloc(new_args, new_capacity * sizeof(char *));
                            if (!new_args) {
                                perror("realloc expand_globs");
                                exit(EXIT_FAILURE);
                            }
                        }
                        new_args[new_count++] = strdup(g.gl_pathv[j]);
                    }
                }
                globfree(&g);
            } else {
                new_args[new_count++] = strdup(args[i]);
            }
        }
    }
    new_args[new_count] = NULL;
    return new_args;
}

/* ------------------------ */
/* Parse a command string   */
/* ------------------------ */
command_t *parse_command(char *cmd_str) {
    command_t *cmd = malloc(sizeof(command_t));
    if (!cmd) {
        perror("malloc parse_command");
        exit(EXIT_FAILURE);
    }
    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->background = 0;
    
    /* Duplicate the command string because strtok will modify it */
    char *cmd_copy = strdup(cmd_str);
    if (!cmd_copy) {
        perror("strdup");
        free(cmd);
        return NULL;
    }
    
    char **tokens = split_line(cmd_copy, " \t\r\n");
    if (!tokens) {
        free(cmd_copy);
        free(cmd);
        return NULL;
    }
    
    char **args = malloc(MAX_TOKENS * sizeof(char *));
    if (!args) {
        perror("malloc args");
        free(tokens);
        free(cmd_copy);
        free(cmd);
        return NULL;
    }
    int arg_index = 0;
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            i++;
            if (tokens[i] == NULL) {
                fprintf(stderr, "Expected filename after '<'\n");
                break;
            }
            cmd->infile = strdup(tokens[i]);
            if (!cmd->infile) perror("strdup infile");
        } else if (strcmp(tokens[i], ">") == 0) {
            i++;
            if (tokens[i] == NULL) {
                fprintf(stderr, "Expected filename after '>'\n");
                break;
            }
            cmd->outfile = strdup(tokens[i]);
            if (!cmd->outfile) perror("strdup outfile");
        } else if (strcmp(tokens[i], "&") == 0) {
            cmd->background = 1;
        } else {
            args[arg_index++] = strdup(tokens[i]);
        }
    }
    args[arg_index] = NULL;
    cmd->args = args;
    
    /* Perform globbing expansion on the arguments */
    char **old_args = cmd->args;
    cmd->args = expand_globs(cmd->args);
    /* Free the original argument strings and array */
    for (int i = 0; old_args[i] != NULL; i++) {
        free(old_args[i]);
    }
    free(old_args);
    
    free(tokens);
    free(cmd_copy);
    return cmd;
}

/* ------------------------ */
/* Free a command_t         */
/* ------------------------ */
void free_command(command_t *cmd) {
    if (!cmd)
        return;
    if (cmd->args) {
        for (int i = 0; cmd->args[i] != NULL; i++) {
            free(cmd->args[i]);
        }
        free(cmd->args);
    }
    if (cmd->infile)
        free(cmd->infile);
    if (cmd->outfile)
        free(cmd->outfile);
    free(cmd);
}

/* ------------------------ */
/* Execute a single command */
/* ------------------------ */
int execute_command(command_t *cmd) {
    pid_t pid, wpid;
    int status;
    
    pid = fork();
    if (pid == 0) {
        /* Child process */
        if (cmd->infile != NULL) {
            int fd_in = open(cmd->infile, O_RDONLY);
            if (fd_in < 0) {
                perror("open infile");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd_in, STDIN_FILENO) < 0) {
                perror("dup2 infile");
                exit(EXIT_FAILURE);
            }
            close(fd_in);
        }
        if (cmd->outfile != NULL) {
            int fd_out = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("open outfile");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd_out, STDOUT_FILENO) < 0) {
                perror("dup2 outfile");
                exit(EXIT_FAILURE);
            }
            close(fd_out);
        }
        if (execvp(cmd->args[0], cmd->args) == -1) {
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork");
    } else {
        /* Parent process */
        if (!cmd->background) {
            do {
                wpid = waitpid(pid, &status, WUNTRACED);
                if (wpid == -1) {
                    perror("waitpid");
                    break;
                }
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        } else {
            printf("Process running in background with PID %d\n", pid);
        }
    }
    return 1;
}

/* ------------------------ */
/* Execute a pipeline       */
/* ------------------------ */
int execute_pipeline(command_t **cmds, int num_cmds) {
    int i;
    int in_fd = 0;  // Initially, input comes from STDIN
    int fd[2];
    pid_t pid;
    
    for (i = 0; i < num_cmds; i++) {
        if (i < num_cmds - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                return -1;
            }
        }
        pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        } else if (pid == 0) {
            /* Child process */
            if (in_fd != 0) {
                if (dup2(in_fd, STDIN_FILENO) < 0) {
                    perror("dup2 in_fd");
                    exit(EXIT_FAILURE);
                }
                close(in_fd);
            }
            if (i < num_cmds - 1) {
                close(fd[0]);
                if (dup2(fd[1], STDOUT_FILENO) < 0) {
                    perror("dup2 fd[1]");
                    exit(EXIT_FAILURE);
                }
                close(fd[1]);
            }
            /* For the first command, apply input redirection if specified */
            if (i == 0 && cmds[i]->infile != NULL) {
                int fd_in = open(cmds[i]->infile, O_RDONLY);
                if (fd_in < 0) {
                    perror("open infile");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd_in, STDIN_FILENO) < 0) {
                    perror("dup2 infile");
                    exit(EXIT_FAILURE);
                }
                close(fd_in);
            }
            /* For the last command, apply output redirection if specified */
            if (i == num_cmds - 1 && cmds[i]->outfile != NULL) {
                int fd_out = open(cmds[i]->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    perror("open outfile");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd_out, STDOUT_FILENO) < 0) {
                    perror("dup2 outfile");
                    exit(EXIT_FAILURE);
                }
                close(fd_out);
            }
            if (execvp(cmds[i]->args[0], cmds[i]->args) < 0) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else {
            /* Parent process */
            if (in_fd != 0)
                close(in_fd);
            if (i < num_cmds - 1) {
                close(fd[1]);
                in_fd = fd[0];
            }
        }
    }
    /* Wait for all child processes in the pipeline */
    for (i = 0; i < num_cmds; i++) {
        wait(NULL);
    }
    return 1;
}

/* ------------------------ */
/* Main shell loop          */
/* ------------------------ */
int main(void) {
    char *line;
    char **commands;
    
    while (1) {
        printf("utsh$ ");
        fflush(stdout);
        
        line = read_line();
        if (line == NULL) {  // EOF (e.g., Ctrl-D)
            break;
        }
        
        // Skip lines that contain only whitespace.
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strlen(p) == 0) {
            free(line);
            continue;
        }
        
        /* Split the input line into separate commands by ';' */
        commands = split_line(line, ";");
        if (commands == NULL) {
            free(line);
            continue;
        }
        
        for (int i = 0; commands[i] != NULL; i++) {
            char *cmd_str = commands[i];
            // Trim leading whitespace.
            while (*cmd_str == ' ' || *cmd_str == '\t')
                cmd_str++;
            // Remove trailing whitespace (including newlines).
            size_t len = strlen(cmd_str);
            while (len > 0 && (cmd_str[len - 1] == ' ' || cmd_str[len - 1] == '\t' || cmd_str[len - 1] == '\n')) {
                cmd_str[len - 1] = '\0';
                len--;
            }
            if (strlen(cmd_str) == 0)
                continue;
            
            /* If the command is "history", print history and do not add it to the history list. */
            if (strcmp(cmd_str, "history") == 0) {
                print_history();
                continue;
            } else {
                /* For any other command, add it to history. */
                add_history(cmd_str);
            }
            
            /* Check for pipelines */
            if (strchr(cmd_str, '|') != NULL) {
                char **pipe_segments = split_line(cmd_str, "|");
                int num_segments = 0;
                while (pipe_segments[num_segments] != NULL) {
                    num_segments++;
                }
                command_t **pipeline_cmds = malloc(sizeof(command_t*) * num_segments);
                if (!pipeline_cmds) {
                    perror("malloc pipeline_cmds");
                    exit(EXIT_FAILURE);
                }
                for (int j = 0; j < num_segments; j++) {
                    pipeline_cmds[j] = parse_command(pipe_segments[j]);
                    if (!pipeline_cmds[j]) {
                        fprintf(stderr, "Error parsing command in pipeline\n");
                        for (int k = 0; k < j; k++) {
                            free_command(pipeline_cmds[k]);
                        }
                        free(pipeline_cmds);
                        break;
                    }
                }
                execute_pipeline(pipeline_cmds, num_segments);
                for (int j = 0; j < num_segments; j++) {
                    free_command(pipeline_cmds[j]);
                }
                free(pipeline_cmds);
                free(pipe_segments);
            } else {
                /* Single (non-pipeline) command */
                command_t *cmd = parse_command(cmd_str);
                if (!cmd) {
                    fprintf(stderr, "Error parsing command\n");
                    continue;
                }
                /* Built‑in "cd" command */
                if (cmd->args[0] != NULL && strcmp(cmd->args[0], "cd") == 0) {
                    if (cmd->args[1] == NULL) {
                        fprintf(stderr, "cd: expected argument\n");
                    } else {
                        if (chdir(cmd->args[1]) != 0) {
                            perror("cd");
                        }
                    }
                } else {
                    execute_command(cmd);
                }
                free_command(cmd);
            }
        }
        
        free(commands);
        free(line);
    }
    
    /* Free command history */
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }
    free(history);
    
    return 0;
}
