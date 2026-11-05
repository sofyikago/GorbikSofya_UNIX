#include <cerrno>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>

struct File
{
    std::string path;
    dev_t dev;
    ino_t ino;
    std::string digest;
};

static std::unordered_map<off_t, std::vector<File>> kept_by_size;
static int status = 0;

static bool sha1(const std::string& path, std::string& digest)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = ctx != nullptr && EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) == 1;
    if (!ok)
        errno = EIO;

    char buf[65536];
    while (ok) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0)
            break;
        if (EVP_DigestUpdate(ctx, buf, n) != 1) {
            errno = EIO;
            ok = false;
        }
    }

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (ok && EVP_DigestFinal_ex(ctx, md, &len) != 1) {
        errno = EIO;
        ok = false;
    }

    int saved = errno;
    EVP_MD_CTX_free(ctx);
    close(fd);
    errno = saved;

    if (ok)
        digest.assign(reinterpret_cast<char*>(md), len);
    return ok;
}

static bool unchanged(const File& f, off_t size)
{
    struct stat st;
    return lstat(f.path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_dev == f.dev && st.st_ino == f.ino && st.st_size == size;
}

static bool replace_with_link(const File& orig, const File& dup)
{
    std::string tmp = dup.path + ".tmp" + std::to_string(getpid());

    // Without this, a signal between link() and rename() would leave tmp behind.
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGHUP);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGQUIT);
    sigaddset(&block, SIGTERM);
    sigprocmask(SIG_BLOCK, &block, &old);

    bool ok = link(orig.path.c_str(), tmp.c_str()) == 0;
    if (ok && rename(tmp.c_str(), dup.path.c_str()) != 0) {
        int saved = errno;
        unlink(tmp.c_str());
        errno = saved;
        ok = false;
    }

    int saved = errno;
    sigprocmask(SIG_SETMASK, &old, nullptr);
    errno = saved;
    return ok;
}

static int visit(const char* path, const struct stat* st, int type, struct FTW*)
{
    if (type == FTW_NS || type == FTW_DNR) {
        struct stat s;
        if (lstat(path, &s) != 0 && errno == ENOENT)
            return 0;
        std::cerr << path << ": cannot access" << std::endl;
        status = 1;
        return 0;
    }

    // Empty files are skipped: linking them frees no space.
    if (type != FTW_F || !S_ISREG(st->st_mode) || st->st_size == 0)
        return 0;

    off_t size = st->st_size;
    File f{path, st->st_dev, st->st_ino, ""};
    std::vector<File>& group = kept_by_size[size];

    // A file with a size not seen before cannot have a duplicate yet: no hashing.
    if (group.empty()) {
        group.push_back(f);
        return 0;
    }

    for (const File& kept : group)
        if (kept.dev == f.dev && kept.ino == f.ino)
            return 0;

    if (!sha1(f.path, f.digest)) {
        perror(path);
        status = 1;
        return 0;
    }

    for (auto it = group.begin(); it != group.end();) {
        if (it->digest.empty() && !sha1(it->path, it->digest)) {
            perror(it->path.c_str());
            status = 1;
            it = group.erase(it);
        } else {
            ++it;
        }
    }

    std::string other_fs;
    for (File& kept : group) {
        if (kept.digest != f.digest)
            continue;
        if (kept.dev != f.dev) {
            other_fs = kept.path;
            continue;
        }
        if (!unchanged(kept, size)) {
            kept = f;
            return 0;
        }
        if (!unchanged(f, size)) {
            std::cerr << path << ": changed during scan, skipped" << std::endl;
            return 0;
        }

        if (replace_with_link(kept, f)) {
            std::cout << path << " -> " << kept.path << std::endl;
        } else {
            perror(path);
            status = 1;
        }
        return 0;
    }

    if (!other_fs.empty())
        std::cout << path << ": same as " << other_fs
                  << ", but on another filesystem, not linked" << std::endl;

    group.push_back(f);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " DIRECTORY" << std::endl;
        return 1;
    }

    if (nftw(argv[1], visit, 64, FTW_PHYS) != 0) {
        perror(argv[1]);
        return 1;
    }

    return status;
}
