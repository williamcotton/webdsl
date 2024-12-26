#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

typedef enum {
    TOKEN_WEBSITE,
    TOKEN_PAGES,
    TOKEN_PAGE,
    TOKEN_STYLES,
    TOKEN_ROUTE,
    TOKEN_LAYOUT,
    TOKEN_CONTENT,
    TOKEN_NAME,
    TOKEN_AUTHOR,
    TOKEN_VERSION,
    TOKEN_ALT,
    TOKEN_LAYOUTS,
    TOKEN_PORT,
    TOKEN_API,
    TOKEN_METHOD,
    TOKEN_RESPONSE,
    TOKEN_NUMBER,

    TOKEN_STRING,
    TOKEN_OPEN_BRACE,
    TOKEN_CLOSE_BRACE,
    TOKEN_OPEN_PAREN,
    TOKEN_CLOSE_PAREN,
    TOKEN_EOF,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    char *lexeme;
    TokenType type;
    int line;
    uint64_t : 64; // Padding to 8 bytes
} Token;

struct Parser;  // Forward declaration

typedef struct Lexer {
    const char *start;   
    const char *current;
    struct Parser *parser;
    int line;
    uint32_t : 32;
} Lexer;

void initLexer(Lexer *lexer, const char *source, struct Parser *parser);
Token getNextToken(Lexer *lexer);
const char* getTokenTypeName(TokenType type);

#endif // LEXER_H
