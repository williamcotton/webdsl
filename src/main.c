#include "parser.h"
#include "server/server.h"
#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include "website.h"

#define MAX_PATH_LENGTH 4096
#define ERROR_MSG_SIZE 256

static volatile int keepRunning = 1;

static void intHandler(int dummy) {
    (void)dummy;
    keepRunning = 0;
}

static time_t getFileModTime(const char* path) {
    struct stat attr;
    if (stat(path, &attr) == 0) {
        return attr.st_mtime;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        char error_msg[ERROR_MSG_SIZE];
        int written = snprintf(error_msg, ERROR_MSG_SIZE, 
            "Usage: %s [path to .webdsl file]\n", argv[0]);
        if (written > 0 && written < ERROR_MSG_SIZE) {
            fputs(error_msg, stderr);
        }
        return 64;
    }

    // Validate path length
    if (strlen(argv[1]) >= MAX_PATH_LENGTH) {
        fputs("Error: File path too long\n", stderr);
        return 64;
    }

    // Set up signal handler for clean shutdown
    signal(SIGINT, intHandler);

    Parser parser = {0};  // Zero initialize all fields
    WebsiteNode* website = NULL;
    time_t lastMod = 0;

    while (keepRunning) {
        time_t currentMod = getFileModTime(argv[1]);
        
        if (currentMod > lastMod) {
            printf("\nReloading website configuration...\n");
            website = reloadWebsite(&parser, website, argv[1]);
            if (website != NULL) {
                lastMod = currentMod;
            }
        }
        
        usleep(100000);
    }

    // Cleanup
    stopServer();
    if (parser.arena != NULL) {
        freeArena(parser.arena);
    }

    printf("\nShutdown complete\n");
    return 0;
}
