#ifndef website_h
#define website_h

#include "parser.h"

// Reload website configuration from file
WebsiteNode* reloadWebsite(Parser *parser, WebsiteNode *website, const char *filename);

#endif
