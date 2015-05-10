#include "os.hpp"
#include "util.hpp"
#include "random.hpp"
#include "error.h"
#include "glfw.hpp"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

static RandomState random_state;
static const mode_t default_dir_mode = 0777;

ByteBuffer os_get_home_dir(void) {
    const char *env_home_dir = getenv("HOME");
    if (env_home_dir)
        return env_home_dir;
    struct passwd *pw = getpwuid(getuid());
    return pw->pw_dir;
}

ByteBuffer os_get_app_dir(void) {
    return os_path_join(os_get_home_dir(), ".genesis");
}

ByteBuffer os_get_projects_dir(void) {
    return os_path_join(os_get_app_dir(), "projects");
}

ByteBuffer os_get_samples_dir(void) {
    return os_path_join(os_get_app_dir(), "samples");
}

ByteBuffer os_get_app_config_dir(void) {
    return os_get_app_dir();
}

ByteBuffer os_get_app_config_path(void) {
    return os_path_join(os_get_app_config_dir(), "config");
}

static int get_random_seed(uint32_t *seed) {
    int fd = open("/dev/random", O_RDONLY|O_NONBLOCK);
    if (fd == -1)
        return GenesisErrorSystemResources;

    int amt = read(fd, seed, 4);
    if (amt != 4) {
        close(fd);
        return GenesisErrorSystemResources;
    }

    close(fd);
    return 0;
}

uint32_t os_random_uint32(void) {
    return get_random(&random_state);
}

uint64_t os_random_uint64(void) {
    uint32_t buf[2];
    buf[0] = os_random_uint32();
    buf[1] = os_random_uint32();
    uint64_t *ptr = reinterpret_cast<uint64_t *>(buf);
    return *ptr;
}

double os_random_double(void) {
    uint32_t x = os_random_uint32();
    return ((double)x) / (((double)(UINT32_MAX))+1);
}

void os_init(OsRandomQuality random_quality) {
    uint32_t seed;
    switch (random_quality) {
    case OsRandomQualityRobust:
        {
            int err = get_random_seed(&seed);
            if (err)
                panic("Unable to get random seed: %s", genesis_error_string(err));
            break;
        }
    case OsRandomQualityPseudo:
        seed = time(nullptr) + getpid();
        break;
    }
    init_random_state(&random_state, seed);
}

void os_spawn_process(const char *exe, const List<ByteBuffer> &args, bool detached) {
    pid_t pid = fork();
    if (pid == -1)
        panic("fork failed");
    if (pid != 0)
        return;
    if (detached) {
        if (setsid() == -1)
            panic("process detach failed");
    }

    const char **argv = allocate<const char *>(args.length() + 2);
    argv[0] = exe;
    argv[args.length() + 1] = nullptr;
    for (int i = 0; i < args.length(); i += 1) {
        argv[i + 1] = args.at(i).raw();
    }
    execvp(exe, const_cast<char * const *>(argv));
    panic("execvp failed: %s", strerror(errno));
}

void os_open_in_browser(const String &url) {
    List<ByteBuffer> args;
    ok_or_panic(args.append(url.encode()));
    os_spawn_process("xdg-open", args, true);
}

double os_get_time(void) {
    struct timespec tms;
    clock_gettime(CLOCK_MONOTONIC, &tms);
    double seconds = (double)tms.tv_sec;
    seconds += ((double)tms.tv_nsec) / 1000000000.0;
    return seconds;
}

String os_get_user_name(void) {
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "Unknown User";
}

int os_delete(const char *path) {
    if (unlink(path))
        return GenesisErrorFileAccess;
    return 0;
}

int os_rename_clobber(const char *source, const char *dest) {
    return rename(source, dest) ? GenesisErrorFileAccess : 0;
}

int os_create_temp_file(const char *dir, OsTempFile *out_tmp_file) {
    out_tmp_file->path = os_path_join(dir, "XXXXXX");
    int fd = mkstemp(out_tmp_file->path.raw());
    if (fd == -1)
        return GenesisErrorFileAccess;
    out_tmp_file->file = fdopen(fd, "w+");
    if (!out_tmp_file->file) {
        close(fd);
        return GenesisErrorNoMem;
    }
    return 0;
}

int os_mkdirp(ByteBuffer path) {
    struct stat st;
    int err = stat(path.raw(), &st);
    if (!err && S_ISDIR(st.st_mode))
        return 0;

    err = mkdir(path.raw(), default_dir_mode);
    if (!err)
        return 0;
    if (errno != ENOENT)
        return GenesisErrorFileAccess;

    ByteBuffer dirname = os_path_dirname(path);
    err = os_mkdirp(dirname);
    if (err)
        return err;

    return os_mkdirp(path);
}

ByteBuffer os_path_dirname(ByteBuffer path) {
    ByteBuffer result;
    const char *ptr = path.raw();
    const char *last_slash = NULL;
    while (*ptr) {
        const char *next = ptr + 1;
        if (*ptr == '/' && *next)
            last_slash = ptr;
        ptr = next;
    }
    if (!last_slash) {
        if (path.at(0) == '/')
            result.append("/", 1);
        return result;
    }
    ptr = path.raw();
    while (ptr != last_slash) {
        result.append(ptr, 1);
        ptr += 1;
    }
    if (result.length() == 0 && path.at(0) == '/')
        result.append("/", 1);
    return result;
}

ByteBuffer os_path_join(ByteBuffer left, ByteBuffer right) {
    const char *fmt_str = (left.at(left.length() - 1) == '/') ? "%s%s" : "%s/%s";
    return ByteBuffer::format(fmt_str, left.raw(), right.raw());
}

int os_readdir(const char *dir, List<OsDirEntry*> &entries) {
    for (int i = 0; i < entries.length(); i += 1)
        os_dir_entry_unref(entries.at(i));
    entries.clear();

    DIR *dp = opendir(dir);
    if (!dp) {
        switch (errno) {
            case EACCES:
                return GenesisErrorPermissionDenied;
            case EMFILE: // fall through
            case ENFILE:
                return GenesisErrorSystemResources;
            case ENOENT:
                return GenesisErrorFileNotFound;
            case ENOMEM:
                return GenesisErrorNoMem;
            case ENOTDIR:
                return GenesisErrorNotDir;
            default:
                panic("unexpected error from opendir: %s", strerror(errno));
        }
    }
    struct dirent *ep;
    while ((ep = readdir(dp))) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0)
            continue;
        ByteBuffer full_path = os_path_join(dir, ep->d_name);
        struct stat st;
        if (stat(full_path.raw(), &st)) {
            int c_err = errno;
            switch (c_err) {
                case EACCES:
                    closedir(dp);
                    return GenesisErrorPermissionDenied;
                case ELOOP:
                case ENAMETOOLONG:
                case EOVERFLOW:
                    closedir(dp);
                    return GenesisErrorUnimplemented;
                case ENOENT:
                case ENOTDIR:
                    // we can simply skip this entry
                    continue;
                case ENOMEM:
                    closedir(dp);
                    return GenesisErrorNoMem;
                default:
                    panic("unexpected error from stat: %s", strerror(errno));
            }
        }
        OsDirEntry *entry = create_zero<OsDirEntry>();
        if (!entry) {
            closedir(dp);
            return GenesisErrorNoMem;
        }
        entry->name = ByteBuffer(ep->d_name);
        entry->is_dir = S_ISDIR(st.st_mode);
        entry->is_file = S_ISREG(st.st_mode);
        entry->is_link = S_ISLNK(st.st_mode);
        entry->is_hidden = ep->d_name[0] == '.';
        entry->size = st.st_size;
        entry->mtime = st.st_mtime;
        entry->ref_count = 1;

        if (entries.append(entry)) {
            closedir(dp);
            os_dir_entry_unref(entry);
            return GenesisErrorNoMem;
        }
    }
    closedir(dp);
    return 0;
}

void os_dir_entry_ref(OsDirEntry *dir_entry) {
    dir_entry->ref_count += 1;
}

void os_dir_entry_unref(OsDirEntry *dir_entry) {
    if (dir_entry) {
        dir_entry->ref_count -= 1;
        assert(dir_entry->ref_count >= 0);
        if (dir_entry->ref_count == 0)
            destroy(dir_entry, 1);
    }
}
