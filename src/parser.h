#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include "arena.h"

typedef struct Parser {
    Lexer lexer;
    Token current;
    Token previous;
    Arena *arena;
    int hadError;
    uint32_t : 32;
} Parser;

void initParser(Parser *parser, const char *source);
WebsiteNode *parseProgram(Parser *parser);

#endif // PARSER_H
