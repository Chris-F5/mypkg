/*
 * usage:
 *   mypkg [package directory] [target directory]
 */

#define DEFAULT_PACKAGE_DIR "."
#define DEFAULT_TARGET_DIR "/"

#define PACKAGE_INFO_FNAME "pkginfo"
#define PACKAGE_FILES_DIRNAME "pkgfiles"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void die(int status) {
    fprintf(stderr, "EXIT (%d)\n", status);
    exit(status);
}

char *combinePath(char* a, char* b)
{
    char *out;
    int a_len, b_len, out_len;
    int i, j;

    if(a == NULL || b == NULL) {
        fprintf(stderr, "combinePath does not accept empty strings\n");
        return NULL;
    }

    a_len = strlen(a);
    b_len = strlen(b);
    if(a_len == 0 || b_len < 0) {
        fprintf(stderr, "cant combine empty paths\n");
        return NULL;
    }
    out_len = a_len
        + (a[a_len - 1] == '/' ? 0 : 1)
        + b_len;
    out = malloc(out_len + 1);
    if(out == NULL) {
        char *err;
        err = strerror(errno);
        fprintf(stderr, "malloc failed (%s)\n", err);
        return NULL;
    }
    i = j = 0;
    while(j < a_len)
        out[i++] = a[j++];
    if(out[i - 1] != '/')
        out[i++] = '/';
    j = 0;
    while(j < b_len)
        out[i++] = b[j++];
    out[i] = '\0';
    if(i != out_len) {
        fprintf(stderr, "unexpected error in combinePath\n");
        return NULL;
    }
    return out;
}

char *relPath(char *src, char *dst)
{
    int src_len, dst_len;

    if(src == NULL || dst == NULL) {
        fprintf(stderr, "relPath does not accept empty strings\n");
        return NULL;
    }

    src_len = strlen(src);
    dst_len = strlen(dst);

    if(src_len < 1 || dst_len < 1) {
        fprintf(stderr, "relPath takes absolute paths\n");
        return NULL;
    }
    if(src[0] != '/' || dst[0] != '/') {
        fprintf(stderr, "relPath takes absolute paths\n");
        return NULL;
    }

    int i, last_matching_slash;
    i = 1;
    last_matching_slash = 1;
    while(src[i] == dst[i]) {
        if(src[i] == '/')
            last_matching_slash = i;
        i++;
        if(i >= src_len || i >= dst_len)
            break;
    }

    int ups = 0;
    i = last_matching_slash + 1;
    while(i < src_len) {
        if(src[i] == '/')
            ups++;
        i++;
    }

    int out_len = ups * 3 + dst_len - last_matching_slash - 1;
    char *out;

    out = malloc(out_len + 1);
    if(out == NULL) {
        char *err;
        err = strerror(errno);
        fprintf(stderr, "malloc failed (%s)\n", err);
        return NULL;
    }

    i = 0;
    while(i < ups * 3) {
        out[i] = i % 3 == 2 ? '/' : '.';
        i++;
    }

    int j;
    j = last_matching_slash + 1;
    while(j < dst_len)
        out[i++] = dst[j++];
    out[i] = '\0';
    if(i != out_len) {
        fprintf(stderr, "unexpected error in relPath\n");
        return NULL;
    }

    return out;
}

int stowDir(char* src_dirname, char* dst_dirname)
{
    DIR *dir;
    dir = opendir(src_dirname);
    if(dir == NULL) {
        char *err;
        err = strerror(errno);
        fprintf(stderr,
            "failed to open directory '%s' (%s)\n", src_dirname, err);
        return 1;
    }

    struct dirent *child;
    errno = 0;
    while((child = readdir(dir)) != NULL) {
        if(strcmp(child->d_name, ".") == 0
            || strcmp(child->d_name, "..") == 0)
            continue;
        if(child->d_type == DT_CHR) {
            fprintf(stderr, "character device file not supported. skipping\n");
        } else if(child->d_type == DT_DIR) {
            char *new_src_dirname, *new_dst_dirname;
            int result;
            new_src_dirname = combinePath(src_dirname, child->d_name);
            if(new_src_dirname == NULL)
                return 1;
            new_dst_dirname = combinePath(dst_dirname, child->d_name);
            if(new_dst_dirname == NULL)
                return 1;
            result = stowDir(new_src_dirname, new_dst_dirname);
            free(new_src_dirname);
            free(new_dst_dirname);
            if(result) {
                return 1;
            }
        } else if(child->d_type == DT_FIFO) {
            fprintf(stderr, "fifo file type not supported. skipping\n");
        } else if(child->d_type == DT_LNK) {
            fprintf(stderr, "link files not yet supported. skipping\n");
        } else if(child->d_type == DT_REG) {
            char *src, *dst, *rel;
            src = combinePath(src_dirname, child->d_name);
            if(src == NULL)
                return 1;
            dst = combinePath(dst_dirname, child->d_name);
            if(dst == NULL)
                return 1;

            rel = relPath(dst, src);

            printf("%s -> %s\n", dst, rel);
            free(rel);
            free(src);
            free(dst);
        } else if(child->d_type == DT_SOCK) {
            fprintf(stderr, "socket file  not supported. skipping\n");
        } else if(child->d_type == DT_UNKNOWN) {
            fprintf(stderr,
                "filesystem not supported, does not support file type\n");
            return 1;
        } else {
            fprintf(stderr, "unexpected error! invalid dir type\n");
            return 1;
        }
    }
    if(errno != 0) {
        char *err;
        err = strerror(errno);
        fprintf(stderr,
            "failed to read from package files directory (%s)\n", err);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char *package_dirname, *target_dirname;

    if(argc > 3) {
        fprintf(stderr, "too many args\n");
        die(1);
    } else if (argc == 3) {
        package_dirname = argv[1];
        target_dirname = argv[2];
    } else if(argc == 2) {
        package_dirname = argv[1];
        target_dirname = DEFAULT_TARGET_DIR;
    } else {
        package_dirname = DEFAULT_PACKAGE_DIR;
        target_dirname = DEFAULT_TARGET_DIR;
    }

    package_dirname = realpath(package_dirname, NULL);
    target_dirname = realpath(target_dirname, NULL);

    char *pkginfo_fname, *pkgfiles_dirname;

    pkginfo_fname = combinePath(package_dirname, PACKAGE_INFO_FNAME);
    if(pkginfo_fname == NULL)
        die(1);
    pkgfiles_dirname = combinePath(package_dirname, PACKAGE_FILES_DIRNAME);
    if(pkgfiles_dirname == NULL)
        die(1);

    { /* check package_dirname exists */
        DIR *pkg_dir;
        pkg_dir = opendir(package_dirname);
        if(pkg_dir == NULL) {
            char *err;
            err = strerror(errno);
            fprintf(stderr,
                "failed to open package directory '%s' (%s)\n",
                package_dirname, err);
            die(1);
        }
        closedir(pkg_dir);
    }

    FILE *pkginfo_file;
    /* open package_info_file and check it exists */
    pkginfo_file = fopen(pkginfo_fname, "r");
    if(pkginfo_file == NULL) {
        char *err;
        err = strerror(errno);
        fprintf(stderr,
            "failed to open package file '%s' (%s)\n", pkginfo_fname, err);
        die(1);
    }

    DIR *pkgfiles_dir;
    /* open package files dir and check it exists */
    pkgfiles_dir = opendir(pkgfiles_dirname);
    if(pkgfiles_dir == NULL) {
        char *err;
        err = strerror(errno);
        fprintf(stderr,
            "failed to open package install directory '%s' (%s)\n",
            pkgfiles_dirname, err);
        die(1);
    }

    { /* check target_dirname exists */
        DIR *tgt_dir;
        tgt_dir = opendir(target_dirname);
        if(tgt_dir == NULL) {
            char *err;
            err = strerror(errno);
            fprintf(stderr,
                "failed to open target directory '%s' (%s)\n", target_dirname, err);
            die(1);
            /* TODO: free on die */
        }
        closedir(tgt_dir);
    }

    if(stowDir(pkgfiles_dirname, target_dirname)) {
        /* TODO: undo on error */
    }

    closedir(pkgfiles_dir);
    fclose(pkginfo_file);
    free(pkginfo_fname);
    free(pkgfiles_dirname);
    free(target_dirname);
    free(package_dirname);

    return 0;
}
