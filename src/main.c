#include "parser.h"
#include "server/server.h"
#include "website_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <getopt.h>
#include <jansson.h>
#include "website.h"

#define MAX_PATH_LENGTH 4096
#define MAX_INCLUDES 100  // Maximum number of files to track

typedef struct {
    char filepath[MAX_PATH_LENGTH];
    time_t last_mod_time;
} FileModInfo;

typedef struct {
    FileModInfo files[MAX_INCLUDES];
    int count;
    uint64_t : 32;  // 3 bytes of padding (24 bits)
} ModificationTracker;

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

static void initModTracker(ModificationTracker* tracker, const char* main_file) {
    tracker->count = 1;
    strncpy(tracker->files[0].filepath, main_file, MAX_PATH_LENGTH - 1);
    tracker->files[0].filepath[MAX_PATH_LENGTH - 1] = '\0';
    tracker->files[0].last_mod_time = 0;  // Initialize to 0 to force first load
}

static void updateModTrackerFromWebsite(ModificationTracker* tracker, WebsiteNode* website) {
    // Start at index 1 since main file is at index 0
    tracker->count = 1;
    
    // Add all included files to the tracker
    IncludeNode* current = website->includeHead;
    while (current != NULL && tracker->count < MAX_INCLUDES) {
        strncpy(tracker->files[tracker->count].filepath, current->filepath, MAX_PATH_LENGTH - 1);
        tracker->files[tracker->count].filepath[MAX_PATH_LENGTH - 1] = '\0';
        tracker->files[tracker->count].last_mod_time = getFileModTime(current->filepath);
        tracker->count++;
        current = current->next;
    }
}

static bool checkModifications(ModificationTracker* tracker) {
    bool modified = false;
    for (int i = 0; i < tracker->count; i++) {
        time_t current_mod = getFileModTime(tracker->files[i].filepath);
        if (current_mod > tracker->files[i].last_mod_time) {
            printf("\nFile modified: %s\n", tracker->files[i].filepath);
            tracker->files[i].last_mod_time = current_mod;
            modified = true;
        }
    }
    return modified;
}

static void printUsage(const char* program) {
    fprintf(stderr, "Usage: %s [options] [path to .webdsl file]\n", program);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --json    Parse the .webdsl file and output AST as JSON\n");
    fprintf(stderr, "  --help    Show this help message\n");
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    bool json_output = false;
    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "jh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'j':
                json_output = true;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case '?':
                return 64;
            default:
                break;
        }
    }

    if (optind >= argc) {
        printUsage(argv[0]);
        return 64;
    }

    // Get the .webdsl file path from remaining args
    const char* webdsl_path = argv[optind];

    // Validate path length
    if (strlen(webdsl_path) >= MAX_PATH_LENGTH) {
        fputs("Error: File path too long\n", stderr);
        return 64;
    }

    Parser parser = {0};  // Zero initialize all fields
    WebsiteNode* website = NULL;
    ModificationTracker mod_tracker = {0};

    // If JSON output mode, just parse and output
    if (json_output) {
        website = parseWebsite(&parser, webdsl_path);
        if (website != NULL) {
            char* json_str = websiteToJson(website);
            // char* json_str = json_dumps(json, JSON_INDENT(2));
            if (json_str) {
                printf("%s\n", json_str);
                free(json_str);
            }
            freeArena(parser.arena);
            return 0;
        }
        freeArena(parser.arena);
        return 1;
    }

    // Set up signal handler for clean shutdown
    signal(SIGINT, intHandler);
    
    // Initialize the modification tracker with the main file
    initModTracker(&mod_tracker, webdsl_path);

    while (keepRunning) {
        if (checkModifications(&mod_tracker)) {
            printf("\nReloading website configuration...\n");
            website = reloadWebsite(&parser, website, webdsl_path);
            if (website != NULL) {
                // Update tracker with any new includes from the website
                updateModTrackerFromWebsite(&mod_tracker, website);
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
