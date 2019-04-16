#include <mapreduce/yt/interface/init.h>

extern "C" int (*mainptr)(int argc, char** argv);

static int (*prev_mainptr)(int argc, char** argv);

static int YtMain(int argc, char** argv) {
    NYT::Initialize(argc, const_cast<const char**>(argv));
    return prev_mainptr(argc, argv);
}

static void InitYtMain() {
    prev_mainptr = mainptr;
    mainptr = YtMain;
}

static int initYtMain = (InitYtMain(), 0);
