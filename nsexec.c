#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <syslog.h>
#include <sys/mount.h>
#include <dirent.h>
#include <sys/wait.h>

#define NETNS_RUN_DIR "/var/run/netns"
#define NETNS_ETC_DIR "/etc/netns"
#define MAX_MOUNTS 16

int bind_etc_files(const char *nsname, char mounts[][512], int max) {
    char netns_etc_path[256];
    snprintf(netns_etc_path, sizeof(netns_etc_path), "%s/%s", NETNS_ETC_DIR, nsname);

    DIR *dir = opendir(netns_etc_path);
    if (!dir) return 0;

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL && count < max) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char src[512], dst[512];
        snprintf(src, sizeof(src), "%s/%s", netns_etc_path, entry->d_name);
        snprintf(dst, sizeof(dst), "/etc/%s", entry->d_name);

        struct stat st;
        if (stat(src, &st) == 0) {
            mount("", "/etc", NULL, MS_PRIVATE | MS_REC, NULL);

            if (mount(src, dst, NULL, MS_BIND, NULL) == 0) {
                strncpy(mounts[count], dst, 512);
                count++;
            } else {
                fprintf(stderr, "Failed to bind %s -> %s: %s\n", src, dst, strerror(errno));
            }
        }
    }

    closedir(dir);
    return count;
}

void unmount_all(char mounts[][512], int count) {
    for (int i = 0; i < count; i++) {
        umount2(mounts[i], MNT_DETACH);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <namespace> <program> [args...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *nsname = argv[1];
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NETNS_RUN_DIR, nsname);

    openlog("nsexec", LOG_PID | LOG_CONS, LOG_USER);

    struct stat st;
    if (stat(path, &st) == -1) {
        syslog(LOG_ERR, "Netns '%s' not found in %s: %s", nsname, NETNS_RUN_DIR, strerror(errno));
        fprintf(stderr, "Error: netns '%s' not found in %s (%s)\n", nsname, NETNS_RUN_DIR, strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        syslog(LOG_ERR, "Failed to open netns '%s': %s", path, strerror(errno));
        perror("open netns");
        closelog();
        return EXIT_FAILURE;
    }

    if (unshare(CLONE_NEWNS) == -1) {
        perror("unshare(CLONE_NEWNS)");
        close(fd);
        closelog();
        return EXIT_FAILURE;
    }

    if (setns(fd, CLONE_NEWNET) == -1) {
        syslog(LOG_ERR, "setns failed for '%s': %s", path, strerror(errno));
        perror("setns()");
        close(fd);
        closelog();
        return EXIT_FAILURE;
    }
    close(fd);

    char mounts[MAX_MOUNTS][512];
    int mount_count = bind_etc_files(nsname, mounts, MAX_MOUNTS);

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork() failed");
        unmount_all(mounts, mount_count);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        if (geteuid() == 0 && getuid() != 0) {
            if (setgid(getgid()) != 0) {
                perror("setgid()");
                _exit(126);
            }
            if (setuid(getuid()) != 0) {
                perror("setuid()");
                _exit(126);
            }
        }
        execvp(argv[2], &argv[2]);
        perror("execvp() failed");
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    unmount_all(mounts, mount_count);

    closelog();
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
