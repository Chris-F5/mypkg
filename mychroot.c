#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/mount.h>

#define ENV_VAR_MAX 1024

#define PROC_DIR "/proc"
#define SYS_DIR "/sys"
#define RUN_DIR "/run"
#define DEV_HOST "/dev"
#define DEV_TARGET "/dev"

int fork_exec_wait(char** args, char **environment);

char *DEFAULT_CMD[2] = {"/bin/sh", NULL};

int
fork_exec_wait(char** args, char **environment)
{
    pid_t pid;

    pid = fork();
    if(pid < 0) {
        perror("fork failed");
        return 1;
    }
    if(pid == 0) {
        execve(args[0], args, environment);
        perror("execve failed");
        exit(1);
    }

    if(waitpid(pid, NULL, 0) < 0) {
        perror("waitpid failed");
        return 1;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    int ret, i;
    char *dir_name, **cmd, *new_dev, *term, *term_env, *environment[4];

    ret = 0;
    term_env = new_dev = NULL;

    if(argc < 2) {
        fprintf(stderr, "too few arguments\n");
        ret = 1;
        goto cleanup;
    } if(argc == 2) {
        dir_name = argv[1];
        cmd = DEFAULT_CMD;
    } else {
        dir_name = argv[1];
        cmd = &argv[2];
    }

    new_dev = malloc(PATH_MAX);
    if(new_dev == NULL) {
        perror("malloc failed");
        ret = 1;
        goto cleanup;
    }
    if(snprintf(new_dev, PATH_MAX, "%s%s", dir_name, DEV_TARGET) >= PATH_MAX) {
        fprintf(stderr, "dev path exceeds PATH_MAX\n");
        ret = 1;
        goto cleanup;
    }
    if(mount(DEV_HOST, new_dev, NULL, MS_BIND, NULL) < 0) {
        perror("failed to mount dev");
        ret = 1;
        goto cleanup;
    }

    if(chroot(dir_name) < 0) {
        perror("chroot failed");
        ret = 1;
        goto cleanup;
    }
    if(chdir("/") < 0) {
        perror("failed to set working directory to '/'");
        ret = 1;
        goto cleanup;
    }

    if(mount("none", SYS_DIR, "sysfs", 0, NULL) < 0) {
        perror("failed to mount sysfs");
        ret = 1;
        goto cleanup;
    }
    if(mount("none", PROC_DIR, "proc", 0, NULL) < 0) {
        perror("failed to mount proc");
        ret = 1;
        goto cleanup;
    }
    if(mount("none", RUN_DIR, "tmpfs", 0, NULL) < 0) {
        perror("failed to mount tmpfs");
        ret = 1;
        goto cleanup;
    }

    term = getenv("TERM");;
    if(term != NULL) {
        term_env = malloc(ENV_VAR_MAX);
        if(term_env == NULL) {
            perror("malloc failed");
            ret = 1;
            goto cleanup;
        }
        strcpy(term_env, "TERM=");
        if(strlen(term_env) + strlen(term) >= ENV_VAR_MAX) {
            fprintf(stderr, "TERM environment varable too long\n");
            ret = 1;
            goto cleanup;
        }
        strcat(term_env, term);
    }

    environment[0] = "PS1=(mychroot) \\u:\\w\\$ ";
    environment[1] = "PWD=/";
    environment[2] = term_env; /* possibly NULL */
    environment[3] = NULL;

    fork_exec_wait(cmd, environment);

    if(umount(DEV_TARGET) < 0) {
        perror("failed to unmount tmpfs");
        ret = 1;
        goto cleanup;
    }
    if(umount(SYS_DIR) < 0) {
        perror("failed to unmount sysfs");
        ret = 1;
        goto cleanup;
    }
    if(umount(PROC_DIR) < 0) {
        perror("failed to unmount proc");
        ret = 1;
        goto cleanup;
    }
    if(umount(RUN_DIR) < 0) {
        perror("failed to unmount tmpfs");
        ret = 1;
        goto cleanup;
    }

cleanup:
    free(new_dev);
    free(term_env);
    return ret;
}
