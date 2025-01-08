#ifndef file_utils_h
#define file_utils_h

// Read entire file into memory, returns malloc'd buffer that must be freed
char* readFile(const char* path);

#endif
