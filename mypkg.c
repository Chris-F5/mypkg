/*
 * usage:
 *   mypkg {install/uninstall} [package directory]... [target directory]
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
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

int add_to_buffer(char *new, char *buf, size_t buf_size, int *buf_index);
int path_common_prefix(char *a, char *b);
int path_relative(char *src_dir, char *dst_file, char* buf);
char *remove_prefix(char *prefix, char *string);
int touch_dir(char *dir);
int make_relative_link(char *target, char *link_file);
int copy_link(char *src, char *dst);
int find_recursive(
    char *dir_name,
    int (*handle)(char *, unsigned int, void *),
    void *handle_ctx);
char *str_file_type(unsigned int type);
int install_file(char *src_file, unsigned int type, void *ctx);
int uninstall_link(char *src_file, unsigned int type, void *ctx);
int uninstall_directory(char *src_file, unsigned int type, void *ctx);
int install_pkg(char *pkg_dir, char *install_dir);
int uninstall_pkg(char *pkg_dir, char *install_dir);
int install(char **package_dirs, int package_count, char *install_dir);
int uninstall(char **package_dirs, int package_count, char *install_dir);

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

int
path_relative(char *src_dir, char *dst_file, char* buf)
{
    int ret = 0;
    int common_prefix, buf_i, i;
    char *real_src_dir, *real_dst_file;
    real_src_dir = malloc(PATH_MAX);
    real_dst_file = malloc(PATH_MAX);
    if(real_src_dir == NULL || real_dst_file == NULL) {
        perror("malloc failed");
        ret = 1;
        goto cleanup;
    }

    if(realpath(src_dir, real_src_dir) == NULL) {
        char *err = strerror(errno);
        fprintf(stderr, "failed to get real path of '%s': %s\n", src_dir, err);
        ret = 1;
        goto cleanup;
    }
    if(realpath(dst_file, real_dst_file) == NULL) {
        char *err = strerror(errno);
        fprintf(stderr, "failed to get real path of '%s': %s\n", dst_file, err);
        ret = 1;
        goto cleanup;
    }

    /* add slash to end of real_src_dir */
    i = strlen(real_src_dir);
    if(i >= PATH_MAX - 1) {
        fprintf(stderr, "path exceeds PATH_MAX '%s'\n", real_src_dir);
        ret = 1;
        goto cleanup;
    }
    real_src_dir[i] = '/';
    real_src_dir[i + 1] = '\0';

    common_prefix = path_common_prefix(real_src_dir, real_dst_file);

    buf_i = 0;
    i = common_prefix + 1;
    while(real_src_dir[i]) {
        if(real_src_dir[i] == '/')
            if(add_to_buffer("../", buf, PATH_MAX - 1, &buf_i)) {
                fprintf(stderr, "relative path name exceeds PATH_MAX\n");
                ret = 1;
                goto cleanup;
            }
        i++;
    }

    i = common_prefix + 1;
    if(add_to_buffer(&real_dst_file[i], buf, PATH_MAX - 1, &buf_i)) {
        fprintf(stderr, "relative path name exceeds PATH_MAX\n");
        ret = 1;
        goto cleanup;
    }
    buf[buf_i] = '\0';

cleanup:
    free(real_src_dir);
    free(real_dst_file);
    return ret;
}

char *
remove_prefix(char *prefix, char *string)
{
    while(*prefix != '\0') {
        if(*prefix != *string)
            return NULL;
        if(*string == '\0')
            return NULL;
        string++;
        prefix++;
    }
    return string;
}

int
touch_dir(char *dir)
{
    /* check status of dir */
    struct stat dir_stat;
    if(stat(dir, &dir_stat)) {
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
make_relative_link(char *target, char *link_file)
{
    int ret = 0;
    char *link_file_copy, *rel_path, *link_dir;
    link_file_copy = malloc(PATH_MAX);
    rel_path = malloc(PATH_MAX);
    if(rel_path == NULL || link_file_copy == NULL) {
        perror("malloc failed");
        ret = 1;
        goto cleanup;
    }
    if(strlen(link_file) >= PATH_MAX) {
        fprintf(stderr, "file exceeds PATH_MAX '%s'\n", link_file);
        ret = 1;
        goto cleanup;
    }
    strcpy(link_file_copy, link_file);
    link_dir = dirname(link_file_copy);
    if(path_relative(link_dir, target, rel_path)) {
        ret = 1;
        goto cleanup;
    }
    if(symlink(rel_path, link_file)) {
        char *err = strerror(errno);
        fprintf(stderr,
            "failed to create symbolic link '%s' -> '%s' (%s)\n",
            link_file, rel_path, err);
        ret = 1;
        goto cleanup;
    }
cleanup:
    free(link_file_copy);
    free(rel_path);
    return ret;
}

int
copy_link(char *src, char *dst)
{
    int ret = 0;
    int link_len;
    char *link;

    link = malloc(PATH_MAX);
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
find_recursive(
    char *dir_name,
    int (*handle)(char *, unsigned int, void *),
    void *handle_ctx)
{
    int ret = 0;
    char *file_name;
    DIR *dir;

    /* allocate path strings */
    file_name = malloc(PATH_MAX);
    if (file_name == NULL) {
      perror("malloc failed");
      ret = 1;
      goto cleanup;
    }

    /* open dir */
    dir = opendir(dir_name);
    if (dir == NULL) {
      char *err = strerror(errno);
      fprintf(stderr, "failed to open directory '%s' (%s)\n", dir_name, err);
      ret = 1;
      goto cleanup;
    }

    struct dirent *file;
    errno = 0;
    while ((file = readdir(dir)) != NULL) {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (snprintf(file_name, PATH_MAX, "%s/%s", dir_name, file->d_name)
                >= PATH_MAX) {
            ret = 1;
            fprintf(stderr, "file exceeded PATH_MAX in '%s'\n", dir_name);
            goto cleanup;
        }
        if (handle(file_name, file->d_type, handle_ctx)) {
            ret = 1;
            goto cleanup;
        }
        if (file->d_type == DT_DIR) {
            if (find_recursive(file_name, handle, handle_ctx)) {
                ret = 1;
                goto cleanup;
            }
        }
        errno = 0;
    }

cleanup:
    free(file_name);
    closedir(dir);
    return ret;
}

char *
str_file_type(unsigned int type)
{
    switch(type) {
    case DT_DIR:
        return "directory";
    case DT_LNK:
        return "symbolic link";
    case DT_REG:
        return "regular file";
    case DT_BLK:
        return "block device file";
    case DT_CHR:
        return "character device file";
    case DT_FIFO:
        return "fifo file";
    case DT_SOCK:
        return "socket";
    case DT_UNKNOWN:
        return "unknown";
    default:
        return "invalid file type identifier";
    }
}

int
install_file(char *src_file, unsigned int type, void *ctx)
{
    int ret = 0;
    char *src_dir, *dst_dir;
    char *file_name, *dst_file;

    src_dir = ((char **)ctx)[0];
    dst_dir = ((char **)ctx)[1];

    dst_file = malloc(PATH_MAX);
    if(dst_file == NULL) {
        ret = 1;
        perror("malloc failed");
        goto cleanup;
    }

    file_name = remove_prefix(src_dir, src_file);
    if(file_name == NULL) {
        fprintf(stderr, "src_file does not begin with src_dir\n");
        ret = 1;
        goto cleanup;
    }
    while(*file_name == '/') file_name++;

    if(snprintf(dst_file, PATH_MAX, "%s/%s", dst_dir, file_name) >= PATH_MAX) {
        fprintf(stderr, "path exceeds PATH_MAX somewhere in '%s'\n", dst_dir);
        ret = 1;
        goto cleanup;
    }

    switch(type) {
    case DT_DIR:
        if(touch_dir(dst_file)) {
            fprintf(stderr, "failed to create directory '%s'\n", dst_file);
            ret = 1;
            goto cleanup;
        }
        break;
    case DT_LNK:
        if(copy_link(src_file, dst_file)) {
            fprintf(stderr, "failed to copy link to '%s'\n", dst_file);
            ret = 1;
            goto cleanup;
        }
        break;
    case DT_REG:
        if(make_relative_link(src_file, dst_file)) {
            fprintf(stderr, "failed to make link '%s'\n", dst_file);
            ret = 1;
            goto cleanup;
        }
        break;
    case DT_UNKNOWN:
        fprintf(stderr,
            "unknown file type. filesystem not supported. skipping\n");
        break;
    default:
        fprintf(stderr, "install does not support %s. skipping\n",
            str_file_type(type));
        break;
    }

cleanup:
    free(dst_file);
    return ret;
}

int
uninstall_link(char *src_file, unsigned int type, void *ctx)
{
    int ret = 0;
    int link_len;
    char *src_dir, *dst_dir, *file_name;
    char *dst_file, *dst_file_dir, *correct_link, *found_link;

    src_dir = ((char **)ctx)[0];
    dst_dir = ((char **)ctx)[1];

    dst_file = malloc(PATH_MAX);
    correct_link = malloc(PATH_MAX);
    found_link = malloc(PATH_MAX);
    dst_file_dir = malloc(PATH_MAX);
    if(dst_file == NULL || correct_link == NULL || found_link == NULL
        || dst_file_dir == NULL) {
        ret = 1;
        perror("malloc failed");
        goto cleanup;
    }

    file_name = remove_prefix(src_dir, src_file);
    if(file_name == NULL) {
        fprintf(stderr, "src_file does not begin with src_dir\n");
        ret = 1;
        goto cleanup;
    }
    while(*file_name == '/') file_name++;

    if(snprintf(dst_file, PATH_MAX, "%s/%s", dst_dir, file_name) >= PATH_MAX) {
        fprintf(stderr, "path exceeds PATH_MAX somewhere in '%s'\n", dst_dir);
        ret = 1;
        goto cleanup;
    }

    switch(type) {
    case DT_DIR:
        break;
    case DT_LNK:
        link_len = readlink(src_file, correct_link, PATH_MAX - 1);
        if(link_len < 0) {
            char *err = strerror(errno);
            fprintf(stderr, "failed to read link of '%s': %s\n", src_file, err);
            ret = 1;
            goto cleanup;
        }
        correct_link[link_len] = '\0';
        link_len = readlink(dst_file, found_link, PATH_MAX - 1);
        if(link_len < 0) {
            if(errno == ENOENT)
                break;
            char *err = strerror(errno);
            fprintf(stderr, "failed to read link of '%s': %s\n", dst_file, err);
            ret = 1;
            goto cleanup;
        }
        found_link[link_len] = '\0';
        if(strcmp(correct_link, found_link) != 0) {
            printf("link does not match, skipping '%s'\n", dst_file);
            break;
        }
        if(remove(dst_file)) {
            char *err = strerror(errno);
            fprintf(stderr,
                "failed to remove symbolic link '%s': %s\n", dst_file, err);
        }
        break;
    case DT_REG:
        link_len = readlink(dst_file, found_link, PATH_MAX - 1);
        if(link_len < 0) {
            if(errno == ENOENT)
                break;
            char *err = strerror(errno);
            fprintf(stderr, "failed to read link of '%s': %s\n", dst_file, err);
            ret = 1;
            goto cleanup;
        }
        found_link[link_len] = '\0';
        strcpy(dst_file_dir, dst_file);
        dirname(dst_file_dir);
        if(path_relative(dst_file_dir, src_file, correct_link)) {
            ret = 1;
            goto cleanup;
        }
        if(strcmp(correct_link, found_link) != 0) {
            printf("link points elsewhere, skipping '%s'\n", dst_file);
            break;
        }
        if(remove(dst_file)) {
            char *err = strerror(errno);
            fprintf(stderr, "failed to remove symbolic link '%s': %s\n",
                dst_file, err);
            ret = 1;
            goto cleanup;
        }
        break;
    case DT_UNKNOWN:
        fprintf(stderr,
            "unknown file type. filesystem not supported. skipping\n");
        break;
    default:
        fprintf(stderr, "uninstall does not support %s. skipping\n",
            str_file_type(type));
        break;
    }

cleanup:
    free(dst_file);
    free(correct_link);
    free(found_link);
    free(dst_file_dir);
    return ret;
}

int
uninstall_directory(char *src_file, unsigned int type, void *ctx)
{
    int ret = 0;
    int link_len;
    char *src_dir, *dst_dir, *file_name;
    char *dst_file;

    src_dir = ((char **)ctx)[0];
    dst_dir = ((char **)ctx)[1];

    dst_file = malloc(PATH_MAX);
    if(dst_file == NULL) {
        ret = 1;
        perror("malloc failed");
        goto cleanup;
    }

    file_name = remove_prefix(src_dir, src_file);
    if(file_name == NULL) {
        fprintf(stderr, "src_file does not begin with src_dir\n");
        ret = 1;
        goto cleanup;
    }
    while(*file_name == '/') file_name++;

    if(snprintf(dst_file, PATH_MAX, "%s/%s", dst_dir, file_name) >= PATH_MAX) {
        fprintf(stderr, "path exceeds PATH_MAX somewhere in '%s'\n", dst_dir);
        ret = 1;
        goto cleanup;
    }

    switch(type) {
    case DT_DIR:
        if(rmdir(dst_file) && errno != ENOTEMPTY && errno != ENOENT) {
            char *err = strerror(errno);
            fprintf(stderr,
                "failed to remove directory '%s': %s\n", dst_file, err);
            ret = 1;
            goto cleanup;
        }
        break;
    case DT_LNK:
        break;
    case DT_REG:
        break;
    case DT_UNKNOWN:
        fprintf(stderr,
            "unknown file type. filesystem not supported. skipping\n");
        break;
    default:
        fprintf(stderr, "uninstall does not support %s. skipping\n",
            str_file_type(type));
        break;
    }

cleanup:
    free(dst_file);
    return ret;
}

int
install_pkg(char *pkg_dir, char *install_dir)
{
    int ret = 0;
    char *pkgfiles_dir;

    printf("installing '%s'\n", pkg_dir);

    pkgfiles_dir = malloc(PATH_MAX);
    if(pkgfiles_dir == NULL) {
        perror("malloc failed");
        ret = 1;
        goto cleanup;
    }

    if(snprintf(pkgfiles_dir, PATH_MAX, "%s/%s", pkg_dir, PACKAGE_FILES_DIRNAME)
            >= PATH_MAX) {
        fprintf(stderr,
            "'%s' in '%s' exceeds PATH_MAX\n", PACKAGE_FILES_DIRNAME, pkg_dir);
        ret = 1;
        goto cleanup;
    }
    char *ctx[2];
    ctx[0] = pkgfiles_dir;
    ctx[1] = install_dir;
    if(find_recursive(pkgfiles_dir, install_file, &ctx)) {
        fprintf(stderr, "failed to install files from '%s' to '%s'\n", pkgfiles_dir, install_dir);
        ret = 1;
        goto cleanup;
    }

cleanup:
    free(pkgfiles_dir);
    return ret;
}

int
uninstall_pkg(char *pkg_dir, char *install_dir)
{
    int ret = 0;
    char *pkgfiles_dir;

    printf("uninstalling '%s'\n", pkg_dir);

    pkgfiles_dir = malloc(PATH_MAX);
    if(pkgfiles_dir == NULL) {
        perror("malloc failed");
        ret = 1;
        goto cleanup;
    }

    if(snprintf(pkgfiles_dir, PATH_MAX, "%s/%s", pkg_dir, PACKAGE_FILES_DIRNAME)
            >= PATH_MAX) {
        fprintf(stderr,
            "'%s' in '%s' exceeds PATH_MAX\n", PACKAGE_FILES_DIRNAME, pkg_dir);
        ret = 1;
        goto cleanup;
    }

    char *ctx[2];
    ctx[0] = pkgfiles_dir;
    ctx[1] = install_dir;
    if(find_recursive(pkgfiles_dir, uninstall_link, &ctx)) {
        fprintf(stderr, "failed to uninstall files from '%s'\n", install_dir);
        ret = 1;
        goto cleanup;
    }
    if(find_recursive(pkgfiles_dir, uninstall_directory, &ctx)) {
        fprintf(stderr, "failed to uninstall directories from '%s'\n",
            install_dir);
        ret = 1;
        goto cleanup;
    }

cleanup:
    free(pkgfiles_dir);
    return ret;
}

int
install(char **package_dirs, int package_count, char *install_dir)
{
    int ret = 0;
    for(int i = 0; i < package_count; i++)
        if(install_pkg(package_dirs[i], install_dir)) {
            fprintf(stderr,
                "failed to install package '%s'\n", package_dirs[i]);
            ret = 1;
            if(uninstall_pkg(package_dirs[i], install_dir))
                fprintf(stderr, "failed to uninstall package '%s'\n",
                    package_dirs[i]);
        }
    return ret;
}

int
uninstall(char **package_dirs, int package_count, char *install_dir)
{
    int ret = 0;
    for(int i = 0; i < package_count; i++)
        if(uninstall_pkg(package_dirs[i], install_dir)) {
            fprintf(stderr,
                "failed to uninstall package '%s'\n", package_dirs[i]);
            ret = 1;
        }
    return ret;
}

int
main(int argc, char **argv)
{
    int ret = 0;
    char *install_dir, *default_package_dir;
    char **package_dirs;
    int package_count;

    default_package_dir = DEFAULT_PACKAGE_DIR;

    if(argc < 2) {
        fprintf(stderr, "too few arguments\n");
        ret = 1;
        goto done;
    } else if (argc == 2) {
        package_dirs = &default_package_dir;
        package_count = 1;
        install_dir = DEFAULT_INSTALL_DIR;
    } else if(argc == 3) {
        package_dirs = &argv[2];
        package_count = 1;
        install_dir = DEFAULT_INSTALL_DIR;
    } else {
        package_dirs = &argv[2];
        package_count = argc - 3;
        install_dir = argv[argc - 1];
    }

    if(strcmp(argv[1], "install") == 0) {
        if(install(package_dirs, package_count, install_dir))
            ret = 1;
    } else if(strcmp(argv[1], "uninstall") == 0) {
        if(uninstall(package_dirs, package_count, install_dir))
            ret = 1;
    } else {
        fprintf(stderr, "unrecognised subcommand '%s'\n", argv[1]);
        ret = 1;
    }

done:
    printf("DONE (%d)\n", ret);
    return ret;
}
