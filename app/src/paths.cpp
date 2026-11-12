#include "paths.hpp"

#include <borealis/core/logger.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef __PSV__
static const char* kRoot = "ux0:data";
#else
static const char* kRoot = "data";
#endif

namespace paths {

const std::string& miiDir()
{
    static const std::string d = std::string(kRoot) + "/mii";
    return d;
}

const std::string& appDir()
{
    static const std::string d = std::string(kRoot) + "/mii-maker-vita";
    return d;
}

const std::string& datDir()
{
    static const std::string d = appDir() + "/dat";
    return d;
}

static void makeDir(const std::string& path)
{
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        brls::Logger::warning("mkdir {} failed: {}", path, strerror(errno));
}

static bool exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// Moves `from` to `to` unless `to` already exists (rename does not replace
// files on the Vita).
static void moveIfPresent(const std::string& from, const std::string& to)
{
    if (!exists(from) || exists(to))
        return;
    if (std::rename(from.c_str(), to.c_str()) == 0)
        brls::Logger::info("Moved {} to {}", from, to);
    else
        brls::Logger::warning("Could not move {} to {}: {}", from, to, strerror(errno));
}

void prepare()
{
    makeDir(kRoot);
    makeDir(miiDir());
    makeDir(miiDir() + "/export");
    makeDir(appDir());
    makeDir(datDir());

    moveIfPresent(miiDir() + "/author_id.bin", appDir() + "/author_id.bin");
    for (const char* name : { "AFLResHigh_2_3.dat", "AFLResHigh.dat", "FFLResHigh.dat", "FFLResMiddle.dat",
             "FFLResHighLG.dat", "FFLResMiddleLG.dat", "FFLBodyRes.dat" })
        moveIfPresent(miiDir() + "/" + name, datDir() + "/" + name);
}

} // namespace paths
