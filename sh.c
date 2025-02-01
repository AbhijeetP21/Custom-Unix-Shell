#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include <dirent.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define MAX_HISTORY 50
#define MAX_MATCHES 50
#define TOKEN_BUFSIZE 64
#define TOKEN_DELIM " \t\r\n\a"

/* Global history array and count */
char *history[MAX_HISTORY];
int history_count = 0;

/* Termios structure for raw mode */
struct termios orig_termios;

/* Function prototypes */
void enableRawMode();
void disableRawMode();
void add_to_history(char *command);
char *get_history_command(int index);
char **sh_split_line(char *line);
int handle_redirection(char **args);
int sh_execute_simple(char **args);
int sh_execute_logical(char **args);
char *sh_read_line(void);
int autocomplete(char *buffer, int pos);
void sh_loop(void);

/* --- Terminal (raw mode) functions --- */
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Disable echo and canonical mode
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/* --- History functions --- */
void add_to_history(char *command) {
    size_t len = strlen(command);
    if (len > 0 && command[len - 1] == '\n')
        command[len - 1] = '\0';
    if (strlen(command) == 0)
        return;  // Do not save empty commands

    if (history_count < MAX_HISTORY) {
        history[history_count] = strdup(command);
        history_count++;
    } else {
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; i++) {
            history[i - 1] = history[i];
        }
        history[MAX_HISTORY - 1] = strdup(command);
    }
}

/* Retrieve command from history using 1-based index */
char *get_history_command(int index) {
    if (index <= 0 || index > history_count) {
        return NULL;
    }
    return history[index - 1];
}

/* --- Tokenizer --- */
char **sh_split_line(char *line) {
    int bufsize = TOKEN_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "sh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOKEN_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOKEN_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "sh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, TOKEN_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

/* --- I/O Redirection handling ---
   This function scans the argument list for redirection operators.
   For input redirection ("<"), it opens the given file and replaces STDIN.
   For output redirection (">"), it opens/creates the file (truncating it) and replaces STDOUT.
   For append redirection (">>"), it opens/creates the file in append mode.
   The redirection tokens and the filename are removed from the arguments. */
int handle_redirection(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: no file specified for input redirection\n");
                return -1;
            }
            int fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) {
                perror("open");
                return -1;
            }
            if (dup2(fd, STDIN_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
            // Shift tokens to remove the redirection operator and filename
            int j = i;
            while (args[j + 2] != NULL) {
                args[j] = args[j + 2];
                j++;
            }
            args[j] = NULL;
            args[j + 1] = NULL;
            continue; // Recheck at the same index
        } else if (strcmp(args[i], ">>") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: no file specified for output append redirection\n");
                return -1;
            }
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                perror("open");
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
            int j = i;
            while (args[j + 2] != NULL) {
                args[j] = args[j + 2];
                j++;
            }
            args[j] = NULL;
            args[j + 1] = NULL;
            continue;
        } else if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: no file specified for output redirection\n");
                return -1;
            }
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
            int j = i;
            while (args[j + 2] != NULL) {
                args[j] = args[j + 2];
                j++;
            }
            args[j] = NULL;
            args[j + 1] = NULL;
            continue;
        }
        i++;
    }
    return 0;
}

/* --- Execution functions ---
   sh_execute_simple() executes a command segment. It handles background execution,
   pipes, and calls handle_redirection() in the child process before execvp(). */
int sh_execute_simple(char **args) {
    if (args[0] == NULL)
        return 0;

    /* Check for background execution: if the last token is "&" */
    int background = 0;
    int count = 0;
    while (args[count] != NULL)
        count++;
    if (count > 0 && strcmp(args[count - 1], "&") == 0) {
        background = 1;
        args[count - 1] = NULL;
    }

    /* Check for pipe operator "|" */
    int i = 0, pipe_index = -1;
    while (args[i] != NULL) {
        if (strcmp(args[i], "|") == 0) {
            pipe_index = i;
            break;
        }
        i++;
    }

    if (pipe_index != -1) {
        /* Handle pipe: split the command into two segments */
        args[pipe_index] = NULL;
        char **left_cmd = args;
        char **right_cmd = &args[pipe_index + 1];

        int fd[2];
        if (pipe(fd) == -1) {
            perror("pipe");
            return 1;
        }

        pid_t pid1 = fork();
        if (pid1 == 0) {
            /* In left child, process any redirection in left_cmd */
            if (handle_redirection(left_cmd) < 0)
                exit(EXIT_FAILURE);
            close(fd[0]); // Close unused read end
            dup2(fd[1], STDOUT_FILENO); // Redirect stdout to pipe
            close(fd[1]);
            if (execvp(left_cmd[0], left_cmd) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }

        pid_t pid2 = fork();
        if (pid2 == 0) {
            /* In right child, process any redirection in right_cmd */
            if (handle_redirection(right_cmd) < 0)
                exit(EXIT_FAILURE);
            close(fd[1]); // Close unused write end
            dup2(fd[0], STDIN_FILENO); // Redirect stdin from pipe
            close(fd[0]);
            if (execvp(right_cmd[0], right_cmd) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }

        close(fd[0]);
        close(fd[1]);
        if (!background) {
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);
        } else {
            printf("[Background pid %d]\n", pid1);
            printf("[Background pid %d]\n", pid2);
        }
    } else {
        /* No pipe – simply fork and execute the command */
        pid_t pid = fork();
        if (pid == 0) {
            /* In child, handle any I/O redirection before executing */
            if (handle_redirection(args) < 0)
                exit(EXIT_FAILURE);
            if (execvp(args[0], args) == -1) {
                perror("execvp");
            }
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork");
        } else {
            if (!background) {
                waitpid(pid, NULL, 0);
            } else {
                printf("[Background pid %d]\n", pid);
            }
        }
    }
    return 0;
}

/* sh_execute_logical() handles logical operators (&& and ||)
   by splitting the tokenized command into segments and executing them conditionally. */
int sh_execute_logical(char **args) {
    int start = 0, ret = 0, i = 0;
    while (args[start] != NULL) {
        i = start;
        char *op = NULL;
        /* Find the next logical operator */
        while (args[i] != NULL && strcmp(args[i], "&&") != 0 && strcmp(args[i], "||") != 0) {
            i++;
        }
        if (args[i] != NULL) {
            op = args[i];
            args[i] = NULL;  // Terminate the current segment
        }
        ret = sh_execute_simple(&args[start]);
        if (op == NULL)
            break;
        if (strcmp(op, "&&") == 0) {
            if (ret != 0)
                break;
        } else if (strcmp(op, "||") == 0) {
            if (ret == 0)
                break;
        }
        start = i + 1;
    }
    return ret;
}

/* --- Input (line reading) with Tab Autocompletion ---
   This function reads input character-by-character in raw mode.
   It handles backspaces and intercepts TAB (for autocompletion) until a newline is entered. */
char *sh_read_line(void) {
    enableRawMode();
    int bufsize = BUFFER_SIZE;
    char *buffer = malloc(bufsize);
    int pos = 0;
    if (!buffer) {
        fprintf(stderr, "sh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    while (1) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0)
            break;
        if (c == '\r' || c == '\n') {
            buffer[pos] = '\0';
            printf("\n");
            break;
        } else if (c == 127 || c == '\b') {  // Handle Backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c == '\t') {  // Handle Tab for autocompletion
            pos = autocomplete(buffer, pos);
        } else {
            buffer[pos] = c;
            pos++;
            putchar(c);
            fflush(stdout);
        }
        if (pos >= bufsize - 1) {
            bufsize += BUFFER_SIZE;
            buffer = realloc(buffer, bufsize);
            if (!buffer) {
                fprintf(stderr, "sh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    disableRawMode();
    return buffer;
}

/* --- Tab Autocompletion ---
   When the user presses TAB, this function finds the current word (characters since the last space)
   and scans the current directory for matching filenames. If one match is found, the word is auto‑completed;
   if multiple matches are found, they are listed. */
int autocomplete(char *buffer, int pos) {
    int start = pos - 1;
    while (start >= 0 && buffer[start] != ' ')
        start--;
    start++;
    char partial[BUFFER_SIZE];
    int len = pos - start;
    strncpy(partial, buffer + start, len);
    partial[len] = '\0';

    DIR *dir = opendir(".");
    if (!dir)
        return pos;
    struct dirent *entry;
    int match_count = 0;
    char *matches[MAX_MATCHES];

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, partial, strlen(partial)) == 0) {
            matches[match_count] = strdup(entry->d_name);
            match_count++;
            if (match_count >= MAX_MATCHES)
                break;
        }
    }
    closedir(dir);

    if (match_count == 0) {
        return pos;
    } else if (match_count == 1) {
        char *match = matches[0];
        int match_len = strlen(match);
        for (int i = len; i < match_len; i++) {
            buffer[pos] = match[i];
            pos++;
            putchar(match[i]);
        }
        fflush(stdout);
    } else {
        printf("\n");
        for (int i = 0; i < match_count; i++) {
            printf("%s\t", matches[i]);
        }
        printf("\nutsh$ %s", buffer);
        fflush(stdout);
    }

    for (int i = 0; i < match_count; i++) {
        free(matches[i]);
    }
    return pos;
}

/* --- Main Shell Loop ---
   The shell prompt is printed, input is read (with history and autocompletion support),
   tokenized, and executed (with support for redirection, pipes, background execution,
   and logical operators). */
void sh_loop(void) {
    char *line;
    char **args;
    int status;

    do {
        printf("utsh$ ");
        fflush(stdout);
        line = sh_read_line();

        /* Check for history invocation: if the command starts with "!" followed by a digit */
        if (line[0] == '!' && isdigit(line[1])) {
            int cmd_num = atoi(&line[1]);
            char *history_command = get_history_command(cmd_num);
            if (history_command != NULL) {
                free(line);
                line = strdup(history_command);
                printf("%s\n", line);
            } else {
                fprintf(stderr, "No such command in history.\n");
                free(line);
                continue;
            }
        }

        add_to_history(line);
        args = sh_split_line(line);
        status = sh_execute_logical(args);

        free(line);
        free(args);
    } while (status >= 0);
}

int main() {
    sh_loop();
    return EXIT_SUCCESS;
}