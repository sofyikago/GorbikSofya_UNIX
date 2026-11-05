#include <cerrno>
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
};

static std::vector<File> files;

static int collect(const char* path, const struct stat* st, int type, struct FTW*)
{
    if (type == FTW_F && S_ISREG(st->st_mode))
        files.push_back({path, st->st_dev, st->st_ino});
    else if (type == FTW_DNR || type == FTW_NS)
        std::cerr << path << ": cannot access" << std::endl;
    return 0;
}

static bool sha1(const std::string& path, std::string& digest)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);

    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        EVP_DigestUpdate(ctx, buf, n);
    close(fd);

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, md, &len);
    EVP_MD_CTX_free(ctx);

    if (n < 0)
        return false;
    digest.assign(reinterpret_cast<char*>(md), len);
    return true;
}

static bool replace_with_link(const File& orig, const File& dup)
{
    std::string tmp = dup.path + ".tmp" + std::to_string(getpid());
    if (link(orig.path.c_str(), tmp.c_str()) != 0)
        return false;
    if (rename(tmp.c_str(), dup.path.c_str()) != 0) {
        int saved = errno;
        unlink(tmp.c_str());
        errno = saved;
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " DIRECTORY" << std::endl;
        return 1;
    }

    if (nftw(argv[1], collect, 64, FTW_PHYS | FTW_MOUNT) != 0) {
        perror(argv[1]);
        return 1;
    }

    int status = 0;
    std::unordered_map<std::string, File> kept;

    for (const File& f : files) {
        std::string digest;
        if (!sha1(f.path, digest)) {
            perror(f.path.c_str());
            status = 1;
            continue;
        }

        auto it = kept.find(digest);
        if (it == kept.end()) {
            kept.emplace(digest, f);
            continue;
        }

        const File& orig = it->second;
        if (orig.dev == f.dev && orig.ino == f.ino)
            continue;

        if (replace_with_link(orig, f)) {
            std::cout << f.path << " -> " << orig.path << std::endl;
        } else {
            perror(f.path.c_str());
            status = 1;
        }
    }

    return status;
}
