#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

char* readFile(const char* path) {
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
