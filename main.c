#include <signal.h>
#include <stdio.h>
#include "nullserver.h"

void signalHandler(int sig) {
    (void) sig;
    nullserver_quit();
}

int main(int argc, char* argv[])
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);

    if (!nullserver_create("127.0.0.1", "9090")) {
        return 1;
    }

    int res = nullserver_exec();
    nullserver_destroy();


    return 0;
}
