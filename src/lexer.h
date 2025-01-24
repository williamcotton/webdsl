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
    TOKEN_NAME,
    TOKEN_AUTHOR,
    TOKEN_VERSION,
    TOKEN_ALT,
    TOKEN_LAYOUTS,
    TOKEN_PORT,
    TOKEN_API,
    TOKEN_METHOD,
    TOKEN_EXECUTE_QUERY,
    TOKEN_QUERY,
    TOKEN_SQL,
    TOKEN_DATABASE,
    TOKEN_HTML,
    TOKEN_CSS,
    TOKEN_NUMBER,
    TOKEN_JQ,
    TOKEN_LUA,
    TOKEN_PIPELINE,
    TOKEN_DYNAMIC,
    TOKEN_TRANSFORM,
    TOKEN_SCRIPT,
    TOKEN_EXECUTE_TRANSFORM,
    TOKEN_EXECUTE_SCRIPT,
    TOKEN_INCLUDE,
    TOKEN_ERROR,
    TOKEN_SUCCESS,
    TOKEN_REFERENCE_DATA,
    TOKEN_ENV_VAR,
    TOKEN_AUTH,
    TOKEN_SALT,
    TOKEN_GITHUB,
    TOKEN_CLIENT_ID,
    TOKEN_CLIENT_SECRET,

    TOKEN_STRING,
    TOKEN_OPEN_BRACE,
    TOKEN_CLOSE_BRACE,
    TOKEN_OPEN_PAREN,
    TOKEN_CLOSE_PAREN,
    TOKEN_EOF,
    TOKEN_UNKNOWN,
    TOKEN_RAW_BLOCK,
    TOKEN_RAW_STRING,
    TOKEN_OPEN_BRACKET,
    TOKEN_CLOSE_BRACKET,
    TOKEN_COMMA,
    TOKEN_FIELDS,
    TOKEN_RANGE,
    TOKEN_PARAMS,
    TOKEN_MUSTACHE,
    TOKEN_REDIRECT,
    TOKEN_PARTIAL,
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
    int inBrackets;  // Track if we're inside brackets
    Token previous;  // Track previous token
} Lexer;

void initLexer(Lexer *lexer, const char *source, struct Parser *parser);
Token getNextToken(Lexer *lexer);
const char* getTokenTypeName(TokenType type);

#endif // LEXER_H
