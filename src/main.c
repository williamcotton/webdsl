#include "parser.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

#define MAX_PATH_LENGTH 4096
#define ERROR_MSG_SIZE 256

static char* readFile(const char* path) {
    errno = 0;
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        perror("Could not open file");
        exit(74);
    }

    // Find file size
    if (fseek(file, 0L, SEEK_END) != 0) {
        perror("Failed to seek file");
        fclose(file);
        exit(74);
    }
    
    long fileLength = ftell(file);
    if (fileLength < 0) {
        perror("Failed to get file size");
        fclose(file);
        exit(74);
    }
    
    if (fseek(file, 0L, SEEK_SET) != 0) {
        perror("Failed to seek file");
        fclose(file);
        exit(74);
    }
    
    size_t fileSize = (size_t)fileLength;
    errno = 0;
    char* buffer = malloc(fileSize + 1);
    if (buffer == NULL || errno != 0) {
        perror("Memory allocation failed");
        fclose(file);
        exit(74);
    }

    // Read file
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead < fileSize) {
        perror("Failed to read file");
        free(buffer);
        fclose(file);
        exit(74);
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

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
            
            // Stop current server if running
            if (website != NULL) {
                stopServer();
                website = NULL;
            }

            // Free old arena if it exists
            if (parser.arena != NULL) {
                freeArena(parser.arena);
                parser.arena = NULL;
            }

            // Read and parse file
            char *source = readFile(argv[1]);
            if (source == NULL) {
                char error_msg[ERROR_MSG_SIZE];
                int written = snprintf(error_msg, ERROR_MSG_SIZE, 
                    "Could not read file %s\n", argv[1]);
                if (written > 0 && written < ERROR_MSG_SIZE) {
                    fputs(error_msg, stderr);
                }
                continue;
            }

            // Initialize parser with new source
            initParser(&parser, source);
            website = parseProgram(&parser);
            free(source);  // Free the source after parsing

            if (website != NULL && !parser.hadError) {
                // Handle successful parse
                lastMod = currentMod;
                startServer(website, parser.arena);
                printf("Website reloaded successfully!\n");
            } else {
                fputs("Parsing failed, keeping previous configuration\n", stderr);
            }
        }
        
        usleep(100000);  // Check for changes every 100ms
    }

    // Cleanup
    stopServer();
    if (parser.arena != NULL) {
        freeArena(parser.arena);
    }

    printf("\nShutdown complete\n");
    return 0;
}
