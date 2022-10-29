/*
 * usage:
 *   mypkg [package directory] [target directory]
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_PACKAGE_DIR "."
#define DEFAULT_INSTALL_DIR "/"

#define PACKAGE_INFO_FNAME "pkginfo"
#define PACKAGE_FILES_DIRNAME "pkgfiles"

int
add_to_buffer(char *new, char *buf, size_t buf_size, int *buf_index)
{
    while(*new) {
        if(*buf_index >= buf_size)
            return 1;
        buf[(*buf_index)++] = *new++;
    }
    return 0;
}

char *
path_combine(char *dir, char *file, char *buf)
{
    int i = 0;
    if(add_to_buffer(dir, buf, PATH_MAX, &i)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    if(i > 0 && buf[i - 1] != '/')
        if(add_to_buffer("/", buf, PATH_MAX, &i)) {
            errno = ENAMETOOLONG;
            return NULL;
        }

    if(add_to_buffer(file, buf, PATH_MAX, &i)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    if(i >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    buf[i] = '\0';

    return buf;
}

int
path_common_prefix(char *a, char *b)
{
    int i = 0, ret = 0;

    if(*a != '/' || *b != '/') {
        fprintf(stderr,
            "path_common_prefix takes only absolute paths as input\n");
        return 0;
    }
    a++;
    b++;
    i++;
    while(*a && *b) {
        if(*a != *b)
            break;
        if(*a == '/')
            ret = i;
        a++;
        b++;
        i++;
    }
    return ret;
}

char *
path_relative(char *src, char *dst, char* buf)
{
    int common_prefix = path_common_prefix(src, dst);

    /* jump to after last common slash */
    src += common_prefix + 1;
    dst += common_prefix + 1;

    int i = 0;
    while(*src) {
        if(*src == '/')
            if(add_to_buffer("../", buf, PATH_MAX, &i)) {
                fprintf(stderr, "relative path name exceeds PATH_MAX\n");
                return NULL;
            }
        src++;
    }

    if(add_to_buffer(dst, buf, PATH_MAX, &i)
            || i >= PATH_MAX) {
        fprintf(stderr, "relative path name exceeds PATH_MAX\n");
        return NULL;
    }

    buf[i] = '\0';
    return buf;
}

int
touch_dir(char *dir)
{
    /* check status of dir */
    struct stat dir_stat;
    if(lstat(dir, &dir_stat)) {
        /* if dir does not exist make it */
        if(errno == ENOENT) {
            if(mkdir(dir, 0755)) {
                char *err = strerror(errno);
                fprintf(stderr, "failed to make directory '%s' (%s)\n", dir, err);
                return 1;
            }
            return 0;
        }
        char *err = strerror(errno);
        fprintf(stderr, "failed to stat file '%s' (%s)\n", dir, err);
        return 1;
    }

    /* if non-directory file exists with that name */
    if((dir_stat.st_mode & S_IFDIR) == 0) {
        fprintf(stderr, "file already exists at '%s'\n", dir);
        return 1;
    }

    /* if permissions of the dir are not 755 */
    if((dir_stat.st_mode & 0777) != 0755) {
        fprintf(stderr, "directory has invalid permissions '%s'\n", dir);
        return 1;
    }

    return 0;
}

int
make_link(char *target, char *link_file)
{
    if(symlink(target, link_file)) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to create symbolic link '%s' -> '%s' (%s)\n",
            link_file, target, err);
        return 1;
    }
    return 0;
}

int copy_link(char *src, char *dst)
{
    int ret = 0, link_len;
    char *link = malloc(PATH_MAX);
    if(link == NULL) {
        perror("malloc failed\n");
        ret = 1;
        goto cleanup;
    }
    link_len = readlink(src, link, PATH_MAX - 1);
    if(link_len < 0) {
        char *err = strerror(errno);
        fprintf(stderr, "failed to read link of '%s': %s\n", src, err);
        ret = 1;
        goto cleanup;
    }
    link[link_len] = '\0';
    if(symlink(link, dst)) {
        char *err = strerror(errno);
        fprintf(stderr, "failed to create symlink '%s': %s\n", dst, err);
        ret = 1;
        goto cleanup;
    }
cleanup:
    free(link);
    return ret;
}

int
stow_real_dir(char *real_src_dir, char *real_dst_dir)
{
    int ret = 0;
    char *real_src_file, *real_dst_file;
    DIR *dir;

    /* open src dir */
    dir = opendir(real_src_dir);
    if(dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to open directory '%s' (%s)\n", real_src_dir, err);
        ret = 1;
        goto cleanup;
    }

    /* allocate path strings */
    real_src_file = malloc(PATH_MAX);
    real_dst_file = malloc(PATH_MAX);
    if(real_src_file == NULL || real_dst_dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr, "malloc failed (%s)\n", err);
        ret = 1;
        goto cleanup;
    }

    struct dirent *file;
    errno = 0;
    while((file = readdir(dir)) != NULL) {
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            errno = 0;
            continue;
        }

        /* get this files real src dir */
        real_src_file = path_combine(real_src_dir, file->d_name, real_src_file);
        if(real_src_file == NULL) {
            char *err = strerror(errno);
            fprintf(stderr,
                "failed to get path of file in '%s' (%s)\n",
                real_src_dir, err);
            ret = 1;
            goto cleanup;
        }

        /* get this files real dst dir */
        real_dst_file = path_combine(real_dst_dir, file->d_name, real_dst_file);
        if(real_dst_file == NULL) {
            char *err = strerror(errno);
            fprintf(stderr,
                "failed to generate path of file in '%s' (%s)\n",
                real_dst_dir, err);
            ret = 1;
            goto cleanup;
        }

        if(file->d_type == DT_BLK) {
            printf("does not support block device files yet. skipping\n");
        } else if(file->d_type == DT_CHR) {
            printf("does not support character device files yet. skipping\n");
        } else if(file->d_type == DT_DIR) {
            if(touch_dir(real_dst_file)) {
                fprintf(stderr,
                    "failed to create directory '%s'\n", real_dst_dir);
                ret = 1;
                goto cleanup;
            }
            if(stow_real_dir(real_src_file, real_dst_file)) {
                ret = 1;
                goto cleanup;
            }
        } else if(file->d_type == DT_FIFO) {
            printf("does not support fifo files yet. skipping\n");
        } else if(file->d_type == DT_LNK) {
            if(copy_link(real_src_file, real_dst_file)) {
                fprintf(stderr, "failed to copy link '%s'\n", real_src_file);
                ret = 1;
                goto cleanup;
            }
        } else if(file->d_type == DT_REG) {
            char *rel_path = malloc(PATH_MAX);
            if(rel_path == NULL) {
                fprintf(stderr, "failed to generate relative path\n");
                ret = 1;
                goto cleanup;
            }
            rel_path = path_relative(real_dst_file, real_src_file, rel_path);
            if(rel_path == NULL) {
                fprintf(stderr, "failed to generate relative path\n");
                ret = 1;
                goto cleanup;
            }
            printf("%s -> %s\n", real_dst_file, real_src_file);
            printf("%s\n", rel_path);
            make_link(rel_path, real_dst_file);
        } else if(file->d_type == DT_SOCK) {
            printf("does not support socket files yet. skipping\n");
        } else if(file->d_type == DT_UNKNOWN) {
            fprintf(stderr,
                "unknown file type. filesystem not supported. skipping\n");
        } else {
            fprintf(stderr,
                "unrecognised file type (%u). skipping\n", file->d_type);
        }
        errno = 0;
    }
    if(errno != 0) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to read from directory '%s' (%s)\n", real_src_dir, err);
        ret = 1;
        goto cleanup;
    }

cleanup:
    free(real_src_file);
    free(real_dst_file);
    closedir(dir);
    return ret;
}

int
install_pkg(char *pkg_dir, char *install_dir)
{
    int ret = 0;
    char *real_install_dir, *real_pkgfiles_dir, *old_working_dir;

    /* allocate memory for path strings */
    real_install_dir = malloc(PATH_MAX);
    real_pkgfiles_dir = malloc(PATH_MAX);
    old_working_dir = malloc(PATH_MAX);
    if(real_install_dir == NULL 
            || real_pkgfiles_dir == NULL 
            || old_working_dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr, "malloc failed (%s)\n", err);
        ret = 1;
        goto cleanup;
        return 1;
    }

    /* get real_install_dir */
    real_install_dir = realpath(install_dir, real_install_dir);
    if(real_install_dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to get absolute path for '%s' (%s)\n", install_dir, err);
        ret = 1;
        goto cleanup;
        return 1;
    }

    /* get current working directory */
    old_working_dir = getcwd(old_working_dir, PATH_MAX);
    if(old_working_dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr, "failed to get working directory (%s)\n", err);
        ret = 1;
        goto cleanup;
    }

    /* move working directory into package */
    if(chdir(pkg_dir)) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to enter package directory '%s' (%s)\n", pkg_dir, err);
        ret = 1;
        goto cleanup;
    }

    /* get real_pkg_dir */
    real_pkgfiles_dir = realpath(PACKAGE_FILES_DIRNAME, real_pkgfiles_dir);
    if(real_pkgfiles_dir == NULL) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to get absolute path for '%s' (%s)\n",
            PACKAGE_FILES_DIRNAME, err);
        ret = 1;
        goto cleanup;
    }

    /* stow the directory */
    if(stow_real_dir(real_pkgfiles_dir, real_install_dir)) {
        fprintf(stderr, "failed to stow directory '%s'\n", real_pkgfiles_dir);
        ret = 1;
        goto cleanup;
    }

    /* return to old working directory */
    if(chdir(old_working_dir)) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to return to working directory '%s' (%s)\n",
            old_working_dir, err);
        ret = 1;
        goto cleanup;
    }

cleanup:
    free(old_working_dir);
    free(real_pkgfiles_dir);
    free(real_install_dir);
    return ret;
}

int
main(int argc, char **argv)
{
    char *install_dir, *package_dir;

    if(argc < 1) {
        fprintf(stderr, "too few arguments\n");
        return 1;
    } else if (argc == 1) {
        package_dir = DEFAULT_PACKAGE_DIR;
        install_dir = DEFAULT_INSTALL_DIR;
    } else if(argc == 2) {
        package_dir = argv[1];
        install_dir = DEFAULT_INSTALL_DIR;
    } else if (argc == 3) {
        package_dir = argv[1];
        install_dir = argv[2];
    } else {
        fprintf(stderr, "too many arguments\n");
        return 1;
    }

    if(install_pkg(package_dir, install_dir)) {
        fprintf(stderr, "failed to install package '%s'\n", package_dir);
        return 1;
    }

    return 0;
}
