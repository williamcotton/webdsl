#include "website.h"
#include "server/server.h"
#include "file_utils.h"
#include <stdio.h>

WebsiteNode* parseWebsite(Parser *parser, const char *filename) {
    // Free old arena if it exists
    if (parser->arena != NULL) {
        freeArena(parser->arena);
        parser->arena = NULL;
    }

    // Read and parse file
    char *source = readFile(filename);
    if (source == NULL) {
        fprintf(stderr, "Could not read file %s\n", filename);
        return NULL;
    }

    // Initialize parser with new source
    initParser(parser, source);
    WebsiteNode* website = parseProgram(parser);
    free(source);  // Free the source after parsing

    if (website != NULL && !parser->hadError) {
        return website;
    } else {
        fputs("Parsing failed\n", stderr);
        return NULL;
    }
}

WebsiteNode* reloadWebsite(Parser *parser, WebsiteNode *website, const char *filename) {
    // Stop current server if running
    if (website != NULL) {
        stopServer();
        website = NULL;
    }

    website = parseWebsite(parser, filename);
    if (website != NULL) {
        // Handle successful parse
        startServer(website, parser->arena);
        printf("Website reloaded successfully!\n");
        return website;
    } else {
        fputs("Keeping previous configuration\n", stderr);
        return NULL;
    }
}
