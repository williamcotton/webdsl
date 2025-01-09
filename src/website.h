#ifndef WEBSITE_H
#define WEBSITE_H

#include "parser.h"
#include "ast.h"

// Reload the website configuration and restart the server
WebsiteNode* reloadWebsite(Parser *parser, WebsiteNode *website, const char *filename);

// Parse the website configuration without starting the server
WebsiteNode* parseWebsite(Parser *parser, const char *filename);

#endif // WEBSITE_H
