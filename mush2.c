#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mush.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>
#include <fcntl.h>
#include <errno.h>
#include "mush2.h"
#include "plumbing.h"

int main(int argc, char *argv[])
{
    FILE *r_file = NULL;
    char *line = NULL;
    pipeline pl = NULL;
    int is_batch_mode = 0, ret_val;
    struct sigaction sa;
    sigset_t mask, old;

    /* If there's one arg, go into interactive mode read from stdin */
    if (argc == 1)
    {
        r_file = stdin;
    }
    /* If there's two args, go into batch mode read from given file */
    else if (argc == 2)
    {
        is_batch_mode = 1;
        if (!(r_file = fopen(argv[1], "r")))
        {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
    }
    /* Otherwise, wrong input */
    else
    {
        usage();
    }

    /* Set up sigaction to run handler if program encounters SIGINT from user */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    if ((sigaction(SIGINT, &sa, NULL)) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Infinite loop to keep the shell running */
    while (1)
    {
        /* If the user is in interactive mode, check stdin and stdout */
        if (!is_batch_mode)
        {
            /* Makes sure we are reading from terminal*/
            if (!isatty(STDIN_FILENO))
            {
                /* If we aren't then we have to be in batch mode */
                is_batch_mode = 1;
            }
            /* Makes sure we are outputting into terminal*/
            if (isatty(STDOUT_FILENO))
            {
                /* Prints prompt */
                printf("8-P ");
            }
        }

        /* Parses a line from command line */
        if (!(line = readLongString(r_file)))
        {
            /* If it contains an EOF or if readLongString was interrupted*/
            if (feof(r_file) || errno != EINTR)
            {
                /* If in interactive mode, print new line for formatting
                 * purposes */
                if (!is_batch_mode)
                {
                    printf("\n");
                }
                /* Terminate the shell */
                break;
            }
        }
        /* If readLongString reads a regular line, non-EOF line */
        else
        {
            /* Set up procmask: clear and add SIGINT to mask, then block SIGINT */
            sigemptyset(&mask);
            sigaddset(&mask, SIGINT);
            if (sigprocmask(SIG_BLOCK, &mask, &old) == -1)
            {
                perror("sigprocmask");
                exit(EXIT_FAILURE);
            }

            /* Creates pipeline struct out of command line args*/
            if (!(pl = crack_pipeline(line)))
            {
                /* If it fails, continue to the next iteration*/
                free(line);
                continue;
            }
            else
            {
                /* If the user enters cd, handle it and continue to next
                 * iteration */
                if (strcmp(pl->stage->argv[0], "cd") == 0)
                {
                    handle_cd(pl);
                    free_pipeline(pl);
                    free(line);
                    continue;
                }
                /* If there's only one argument on the command line */
                if (pl->length == 1)
                {
                    ret_val = one_stage_pipeline(pl, mask);
                    /* For debugging */
                    if (ret_val > 0)
                    {
                        /* fprintf(stderr, "Invalid command\n"); */
                    }
                }
                /* If there are more than one command line arguments */
                else
                {
                    ret_val = mult_stage_pipeline(pl, mask);
                    /* For debugging */
                    if (ret_val > 0)
                    {
                        /*fprintf(stderr, "Invalid command(s)\n"); */
                    }
                }
                /* Frees allocated memory */
                free_pipeline(pl);
                free(line);
            }
        }
    }
    /* Cleans up memory from parsing functions */
    yylex_destroy();
    return 0;
}

/* Usage */
void usage()
{
    fprintf(stderr, "usage: \n");
    exit(EXIT_FAILURE);
}

/* If handler ever gets called, we just want to continue running the shell */
void handler(int signum)
{
    printf("\n");
}

/* This function handles if the user enters cd */
void handle_cd(pipeline pl)
{
    char *path = NULL;
    struct passwd *pwent;

    if (pl->stage->argc == 1)
    {
        /* If user doesn't enter an additional argument, then sets path
         * to home directory */
        if (!(path = getenv("HOME")))
        {
            /* If the home directory can't be accessed from getenv, check if
             * it can be accessed from the user's passwd struct */
            if (!(pwent = getpwuid(geteuid())))
            {
                /* If it can't, give up */
                fprintf(stderr, "unable to determine home directory");
            }
            path = pwent->pw_dir;
        }
    }

    /* If user enters an additional argument, then sets path to
     * specified argument */
    if (pl->stage->argc == 2)
    {
        if (pl->stage->argv[1])
        {
            path = pl->stage->argv[1];
        }
    }

    /* Change working directory accordingly */
    if ((chdir(path)) == -1)
    {
        perror(path);
    }
}
