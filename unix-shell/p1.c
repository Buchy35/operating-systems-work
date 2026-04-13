/*

CSC360 - p1 - ssi
Implements a basic shell

Cole Buchinski
V01001066
2025.10.10

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

struct bgjob {
    pid_t pid;
    char command[256]; // store the command + args
    struct bgjob *next;
};

struct bgjob *bg_head = NULL;

#define MAX_ARGS 64

pid_t fg_pid = -1; // -1 = no foreground process running
volatile sig_atomic_t sigint_recieved = 0;


void check_bgjobs() {
    // checks for terminated background jobs ; if job has finished, prints termination message and removes it.
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // find and remove job from list
        struct bgjob **curr = &bg_head;
        while(*curr) {
            if ((*curr)->pid == pid) {
                printf("%d: %s has terminated. \n", pid, (*curr)->command);
                struct bgjob *tmp = *curr;
                *curr = (*curr)->next;
                free(tmp);
                break;
            }
            curr = &(*curr)->next;
        }
    }
}

void handle_sigint(int sig) {
    // handles ctrl-c (SIGINT) ; if fg process running, sends SIGINT to terminate
    if (fg_pid > 0) {
        // there's a foreground process; kill
        kill(fg_pid, SIGINT);
    }

    else {
        // no foreground process - new line
        write(STDOUT_FILENO, "\n", 1);
        sigint_recieved = 1; // redraw prompt
    }
}


int main() {

    signal(SIGINT, handle_sigint);

    char *username = getlogin();
    char hostname[256];
    char cwd[1024];
    char input[1024];

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname");
        exit(1);
    }

    while(1) {

        check_bgjobs();

        // get cwd
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
            exit(1);
        }

        if (sigint_recieved) {
            sigint_recieved = 0;
        }

        // print prompt
        printf("%s@%s: %s > ", username, hostname, cwd);
        fflush(stdout);

        // read input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\nExiting shell...\n");
                break; // ctrl-d (EOF)
            }

            if (ferror(stdin)) {
                clearerr(stdin);
                if (sigint_recieved) {
                    sigint_recieved = 0;

                    if (getcwd(cwd, sizeof(cwd)) != NULL) {
                        printf("%s@%s: %s > ", username, hostname, cwd);
                        fflush(stdout);
                    }
                    continue;
                }
            }
            continue;
        }

        // strip newline
        input[strcspn(input, "\n")] = '\0';

        // skip empty lines
        if (strlen(input) == 0) {
            continue;
        }

        // tokenize input
        char *argv[MAX_ARGS];
        int argc = 0;
        char *token = strtok(input, " \t");
        while (token != NULL && argc < MAX_ARGS - 1) {
            argv[argc++] = token;
            token = strtok(NULL, " \t");
        }
        argv[argc] = NULL;

        if (argc == 0) {
            continue;
        }


        // built ins
        if (strcmp(argv[0], "exit") == 0) {
            break;
        }

        else if (strcmp(argv[0], "cd") == 0) {
            char *target = argv[1];
            if ((!target) || (strcmp(target, "~") == 0)) {
                target = getenv("HOME"); // cd with no args goes home
            }
            
            if (chdir(target) != 0) {
                perror("cd");
            }
            continue;
        }

        else if (strcmp(argv[0], "bg") == 0) {
            if (argc < 2) {
                printf("Usage: bg <command> [args...]\n");
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) {
                execvp(argv[1], &argv[1]); // child runs command
                perror("execvp");
                exit(1);
            }
            
            else if (pid > 0) {
                // parent stores job
                fg_pid = pid;
                struct bgjob *job = malloc(sizeof(struct bgjob));
                job->pid = pid;
                snprintf(job->command, sizeof(job->command), "%s", argv[1]);
                for (int i = 2; i < argc; i++) {
                    strncat(job->command, " ", sizeof(job->command) - strlen(job->command) - 1);
                    strncat(job->command, argv[i], sizeof(job->command) - strlen(job->command) - 1);
                }
                job->next = bg_head;
                bg_head = job;

                printf("Started background job %d: %s\n", pid, job->command);
                fg_pid = -1;
                continue;
            }

            else {
                perror("fork");
            }
        }

        else if (strcmp(argv[0], "bglist") == 0) {
            int count = 0;
            struct bgjob *job = bg_head;
            while (job) {
                printf("%d: %s\n", job->pid, job->command);
                count++;
                job = job->next;
            }
            printf("Total Background Jobs: %d\n", count);
            continue;
        }


        // external command: fork + execvp
        pid_t pid = fork();
        if (pid == 0) {
            // child process
            execvp(argv[0], argv);
            // if execvp fails
            fprintf(stderr, "%s: No such file or directory\n", argv[0]);
            exit(1);
        }

        else if (pid > 0) {
            // parent waits for child
            fg_pid = pid;
            int status;
            waitpid(pid, &status, 0);
            fg_pid = -1;
        }

        else {
            perror("fork");
        }

    }
    return 0;
}