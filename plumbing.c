#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mush.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include "plumbing.h"

#define READ 0
#define WRITE 1
#define PIPE_SIZE 2

/* This function handles the case where there's only one command to exec into */
int one_stage_pipeline(pipeline pl, sigset_t mask)
{
    pid_t child = 0;
    int status, num_errors = 0;
    sigset_t old;

    /* Fork process*/
    if ((child = fork()))
    {
        /* Check for errors*/
        if (child == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        /* Continue parent process */

        /* Unblock SIGINT before wait so wait will function properly */
        if (sigprocmask(SIG_UNBLOCK, &mask, &old) == -1)
        {
            perror("sigprocmask");
            exit(EXIT_FAILURE);
        }

        /* Wait for child to terminate */
        if (wait(&status) == -1)
        {
            /* If child process is interrupted, output error message */
            if (errno != EINTR)
            {
                perror("wait");
            }
        }

        /* Track if child process didn't terminate successfully */
        if (!WIFEXITED(status) || WEXITSTATUS(status))
        {
            num_errors++;
        }
    }
    /* In child process */
    else
    {
        /* If the user was in batch mode, enable change */
        handle_batch_in(pl);
        handle_batch_out(pl);

        /* Unblock SIGINT before execvp so the command can run properly */
        if (sigprocmask(SIG_UNBLOCK, &mask, &old) == -1)
        {
            perror("sigprocmask");
            exit(EXIT_FAILURE);
        }

        /* Exec into command */
        execvp(pl->stage->argv[0], pl->stage->argv);
        perror(pl->stage->argv[0]);
        exit(EXIT_FAILURE);
    }
    return num_errors;
}

/* This function henadles the case where there are multiple commands */
int mult_stage_pipeline(pipeline pl, sigset_t mask)
{
    int **ends;
    pid_t child;
    int procs = pl->length;
    int i, j, status;
    int num_errors = 0;
    sigset_t old;

    /* Allocates array of arrays of size 2 */
    ends = (int **)calloc(procs - 1, sizeof(int *));
    if (!ends)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    /* Allocates arrays of size 2 for n-1 pipes */
    for (i = 0; i < procs - 1; i++)
    {
        ends[i] = calloc(PIPE_SIZE, sizeof(int));
        if (!ends[i])
        {
            perror("calloc");
            exit(EXIT_FAILURE);
        }
    }

    /* Populate arrays of size 2 to n-1 pipes */
    for (i = 0; i < procs - 1; i++)
    {
        if (pipe(ends[i]) == -1)
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < procs; i++)
    {
        /* Fork for each child process */
        if ((child = fork()))
        {
            /* Check if forked successfully */
            if (child == -1)
            {
                perror("fork");
                exit(EXIT_FAILURE);
            }
        }
        /* In child process */
        else
        {
            /* If it's the first process, then we only need to change the
             * WRITE end */
            if (i == 0)
            {
                handle_batch_in(pl);

                /* Set stdout to the write end of pipe */
                if (dup2(ends[i][WRITE], STDOUT_FILENO) == -1)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                /* If we aren't on the first process, then set stdin to the
                 * READ end of to the pipe */
                if (dup2(ends[i - 1][READ], STDIN_FILENO) == -1)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            /* Similarly, if we're at the last process, we only need to change
             * the READ end of the pipe (since WRITE end always has to be
             * stdout) */
            if (i == procs - 1)
            {
                handle_batch_out(pl);
            }

            else
            {
                /* If we aren't on the last process, then set stdout to the
                 * WRITE end of to the pipe */
                if (dup2(ends[i][WRITE], STDOUT_FILENO) == -1)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            /* Close all open file desciptors from pipes */
            for (j = 0; j < procs - 1; j++)
            {
                close(ends[j][READ]);
                close(ends[j][WRITE]);
            }

            /* Unblock SIGINT before execvp so the command can run properly */
            if (sigprocmask(SIG_UNBLOCK, &mask, &old) == -1)
            {
                perror("sigprocmask");
                exit(EXIT_FAILURE);
            }
            /* Run command */
            execvp(pl->stage[i].argv[0], pl->stage[i].argv);
            perror(pl->stage[0].argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Continue parent process */

    /* Parent also has to close all open file descriptors from pipes */
    for (j = 0; j < procs - 1; j++)
    {
        close(ends[j][READ]);
        close(ends[j][WRITE]);
    }

    /* Unblock SIGINT before wait so wait will function properly */
    if (sigprocmask(SIG_UNBLOCK, &mask, &old) == -1)
    {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    /* Wait for all the children to terminate */
    for (j = 0; j < procs; j++)
    {
        if (wait(&status) == -1)
        {
            /* If child process is interrupted, output error message */
            if (errno != EINTR)
            {
                perror("wait");
            }
        }

        /* Track if each child process terminated successfully or not */
        if (!WIFEXITED(status) || WEXITSTATUS(status))
        {
            num_errors++;
        }
    }

    /* Clean up */
    free_ends(ends, procs);
    return num_errors;
}

void handle_batch_in(pipeline pl)
{
    int in_fd;
    /* If user in batch mode provided an input filename */
    if (pl->stage->inname)
    {
        /* Open it */
        if ((in_fd = open(pl->stage->inname, O_RDONLY)) == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);
        }

        /* Set stdin to input filename */
        if (dup2(in_fd, STDIN_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(in_fd);
    }
    /* Otherwise, don't change stdin */
}

void handle_batch_out(pipeline pl)
{
    int out_fd;
    /* If there's an output file in pipeline struct */
    if (pl->stage->outname)
    {
        /* Open it with -rw-rw-rw- perms */
        if ((out_fd = open(pl->stage->outname,
                           O_WRONLY | O_CREAT | O_TRUNC,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
                               S_IROTH | S_IWOTH)) == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);
        }

        /* Set stdout to output filename */
        if (dup2(out_fd, STDOUT_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(out_fd);
    }
    /* Otherwise, don't change stdout */
}

/* This function frees the allocated array and its subarrays */
void free_ends(int **ends, int procs)
{
    int i;
    for (i = 0; i < procs - 1; i++)
    {
        free(ends[i]);
    }
    free(ends);
}
