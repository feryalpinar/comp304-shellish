#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>

#define MAX_ARGS 100
#define MAX_LINE 1024

const char *sysname = "shellish";

/* ================= STRUCT ================= */
struct command_t {
    char *name;
    char *args[MAX_ARGS];
    int arg_count;
    bool background;
    char *redirect_in;
    char *redirect_out;
    char *redirect_append;
    struct command_t *next;
};

/* ================= INIT ================= */

void init_command(struct command_t *cmd) {
    cmd->name = NULL;
    cmd->arg_count = 0;
    cmd->background = false;
    cmd->redirect_in = NULL;
    cmd->redirect_out = NULL;
    cmd->redirect_append = NULL;
    cmd->next = NULL;
}


/* ================= PARSER ================= */

void parse_command(char *line, struct command_t *cmd) {
    init_command(cmd);

    char *token = strtok(line, " \t\n");

    while (token != NULL) {

        if (strcmp(token, "&") == 0) {
            cmd->background = true;
        }

        else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            cmd->redirect_in = token;
        }

        else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            cmd->redirect_out = token;
        }

        else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t\n");
            cmd->redirect_append = token;
        }

        else if (strcmp(token, "|") == 0) {
            cmd->next = malloc(sizeof(struct command_t));
            parse_command(strtok(NULL, "\n"), cmd->next);
            return;
        }

        else {
            if (cmd->name == NULL)
                cmd->name = token;

            cmd->args[cmd->arg_count++] = token;
        }

        token = strtok(NULL, " \t\n");
    }

    cmd->args[cmd->arg_count] = NULL;
}

/* ================= BUILTIN CUT ================= */

void builtin_cut(struct command_t *cmd) {

    char delimiter = '\t';
    char *fields = NULL;

    for (int i = 1; i < cmd->arg_count; i++) {
        if (strcmp(cmd->args[i], "-d") == 0 && i + 1 < cmd->arg_count)
            delimiter = cmd->args[i + 1][0];

        if (strcmp(cmd->args[i], "-f") == 0 && i + 1 < cmd->arg_count)
            fields = cmd->args[i + 1];
    }

    if (!fields) {
        printf("cut: missing -f option\n");
        return;
    }

    int selected[100];
    int count = 0;

    char *fields_copy = strdup(fields);
    char *field_token = strtok(fields_copy, ",");

    while (field_token) {
        selected[count++] = atoi(field_token);
        field_token = strtok(NULL, ",");
    }

    free(fields_copy);

    char delim[2];
    delim[0] = delimiter;
    delim[1] = '\0';

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), stdin)) {

        int field_no = 1;
        char *token = strtok(line, delim);

        while (token) {

            for (int i = 0; i < count; i++) {
                if (selected[i] == field_no)
                    printf("%s", token);
            }

            token = strtok(NULL, delim);
            field_no++;
        }

        printf("\n");
    }
}

/* ================= BUILTIN SYSINFO ================= */

void builtin_sysinfo() {

    printf("User: %s\n", getenv("USER"));

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("Hostname: %s\n", hostname);

    printf("CPU Cores: ");
    system("nproc");

    printf("Memory Info:\n");
    system("free -h");
}

/* ================= BUILTIN CHATROOM ================= */

void builtin_chatroom(char *room, char *username) {

    char room_path[256];
    sprintf(room_path, "/tmp/chatroom-%s", room);

    mkdir(room_path, 0777);

    char user_pipe[256];
    sprintf(user_pipe, "%s/%s", room_path, username);

    mkfifo(user_pipe, 0666);

    printf("Welcome to %s!\n", room);

    pid_t reader = fork();

    if (reader == 0) {
        while (1) {
            int fd = open(user_pipe, O_RDONLY);
            char buffer[1024];
            int n = read(fd, buffer, sizeof(buffer) - 1);

            if (n > 0) {
                buffer[n] = '\0';
                printf("%s", buffer);
                fflush(stdout);
            }

            close(fd);
        }
    }

    while (1) {

        char message[1024];

        printf("[%s] %s > ", room, username);
        fflush(stdout);

        if (!fgets(message, sizeof(message), stdin))
            break;

        DIR *dir = opendir(room_path);
        struct dirent *entry;

        while ((entry = readdir(dir)) != NULL) {

            if (entry->d_type == DT_FIFO &&
                strcmp(entry->d_name, username) != 0) {

                char target_pipe[256];
                sprintf(target_pipe, "%s/%s", room_path, entry->d_name);

                int fd = open(target_pipe, O_WRONLY);
                dprintf(fd, "[%s] %s: %s", room, username, message);
                close(fd);
            }
        }

        closedir(dir);
    }
}

/* ================= EXECUTION ================= */

void execute_command(struct command_t *cmd) {

    if (cmd->name == NULL)
        return;

    /* Builtins */

    if (strcmp(cmd->name, "exit") == 0)
        exit(0);

    if (strcmp(cmd->name, "cd") == 0) {
        if (cmd->args[1])
            chdir(cmd->args[1]);
        return;
    }

    if (strcmp(cmd->name, "cut") == 0) {
        builtin_cut(cmd);
        return;
    }

    if (strcmp(cmd->name, "sysinfo") == 0) {
        builtin_sysinfo();
        return;
    }

    if (strcmp(cmd->name, "chatroom") == 0) {

        if (cmd->arg_count < 3) {
            printf("Usage: chatroom <room> <username>\n");
            return;
        }

        builtin_chatroom(cmd->args[1], cmd->args[2]);
        return;
    }

    /* PIPE */

    if (cmd->next != NULL) {

        int fd[2];
        pipe(fd);

        pid_t pid1 = fork();

        if (pid1 == 0) {
            dup2(fd[1], STDOUT_FILENO);
            close(fd[0]);
            close(fd[1]);
            execute_command(cmd);
            exit(0);
        }

        pid_t pid2 = fork();

        if (pid2 == 0) {
            dup2(fd[0], STDIN_FILENO);
            close(fd[1]);
            close(fd[0]);
            execute_command(cmd->next);
            exit(0);
        }

        close(fd[0]);
        close(fd[1]);
        wait(NULL);
        wait(NULL);
        return;
    }

    /* NORMAL EXEC */

    pid_t pid = fork();

    if (pid == 0) {

        if (cmd->redirect_in) {
            int fd = open(cmd->redirect_in, O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (cmd->redirect_out) {
            int fd = open(cmd->redirect_out,
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (cmd->redirect_append) {
            int fd = open(cmd->redirect_append,
                          O_WRONLY | O_CREAT | O_APPEND, 0644);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        char *path = getenv("PATH");
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");

        while (dir != NULL) {

            char fullpath[512];
            sprintf(fullpath, "%s/%s", dir, cmd->name);

            if (access(fullpath, X_OK) == 0)
                execv(fullpath, cmd->args);

            dir = strtok(NULL, ":");
        }

        printf("-%s: %s: command not found\n", sysname, cmd->name);
        exit(127);
    }

    else {

        if (!cmd->background)
            waitpid(pid, NULL, 0);
        else
            printf("[Background pid %d]\n", pid);
    }
}

/* ================= MAIN ================= */

int main() {

    char line[MAX_LINE];

    while (1) {

        char cwd[256];
        getcwd(cwd, sizeof(cwd));

        printf("%s$ ", cwd);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        struct command_t cmd;
        parse_command(line, &cmd);
        execute_command(&cmd);
    }

    return 0;
}
