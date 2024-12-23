#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>

static int isAlpha(char c) {
    return ((c >= 'a' && c <= 'z') || 
            (c >= 'A' && c <= 'Z') || 
            (c == '_') ||
            (c == '-'));
}

static int isDigit(char c) { 
    return (c >= '0' && c <= '9'); 
}

static int isAtEnd(Lexer *lexer) { 
    return *(lexer->current) == '\0'; 
}

static char advance(Lexer *lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static char peek(Lexer *lexer) { 
    return *(lexer->current); 
}

static char peekNext(Lexer *lexer) {
    if (isAtEnd(lexer)) return '\0';
    return lexer->current[1];
}

static void skipWhitespace(Lexer *lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '\n':
                lexer->line++;
                advance(lexer);
                break;
            case '/':
                if (peekNext(lexer) == '/') {
                    while (peek(lexer) != '\n' && !isAtEnd(lexer)) {
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token makeToken(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    size_t length = (size_t)(lexer->current - lexer->start);
    token.lexeme = arenaAlloc(lexer->parser->arena, length + 1);
    memcpy(token.lexeme, lexer->start, length);
    token.lexeme[length] = '\0';
    token.line = lexer->line;
    return token;
}

static Token errorToken(Parser *parser, const char *message, int line) {
    Token token;
    token.type = TOKEN_UNKNOWN;
    token.lexeme = arenaDupString(parser->arena, message);
    token.line = line;
    return token;
}

static TokenType checkKeyword(const char *start, size_t length) {
#define KW_MATCH(kw, ttype) \
    if (strncmp(start, kw, length) == 0 && strlen(kw) == length) \
        return ttype;

    KW_MATCH("website", TOKEN_WEBSITE)
    KW_MATCH("pages", TOKEN_PAGES)
    KW_MATCH("page", TOKEN_PAGE)
    KW_MATCH("styles", TOKEN_STYLES)
    KW_MATCH("route", TOKEN_ROUTE)
    KW_MATCH("layout", TOKEN_LAYOUT)
    KW_MATCH("content", TOKEN_CONTENT)
    KW_MATCH("name", TOKEN_NAME)
    KW_MATCH("author", TOKEN_AUTHOR)
    KW_MATCH("version", TOKEN_VERSION)
    KW_MATCH("alt", TOKEN_ALT)
    KW_MATCH("layouts", TOKEN_LAYOUTS)
    KW_MATCH("port", TOKEN_PORT)

    return TOKEN_UNKNOWN;
#undef KW_MATCH
}

static Token identifierOrKeyword(Lexer *lexer) {
    while (isAlpha(peek(lexer)) || isDigit(peek(lexer)) || 
           peek(lexer) == '_' || peek(lexer) == '-') {
        advance(lexer);
    }

    size_t length = (size_t)(lexer->current - lexer->start);
    TokenType type = checkKeyword(lexer->start, length);
    if (type == TOKEN_UNKNOWN) {
        type = TOKEN_STRING;
    }
    return makeToken(lexer, type);
}

static Token stringLiteral(Lexer *lexer) {
    while (peek(lexer) != '"' && !isAtEnd(lexer)) {
        if (peek(lexer) == '\n') lexer->line++;
        advance(lexer);
    }

    if (isAtEnd(lexer)) {
        return errorToken(lexer->parser, "Unterminated string.", lexer->line);
    }

    advance(lexer);  // Closing quote
    return makeToken(lexer, TOKEN_STRING);
}

const char* getTokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_WEBSITE: return "WEBSITE";
        case TOKEN_PAGES: return "PAGES";
        case TOKEN_PAGE: return "PAGE";
        case TOKEN_STYLES: return "STYLES";
        case TOKEN_ROUTE: return "ROUTE";
        case TOKEN_LAYOUT: return "LAYOUT";
        case TOKEN_CONTENT: return "CONTENT";
        case TOKEN_NAME: return "NAME";
        case TOKEN_AUTHOR: return "AUTHOR";
        case TOKEN_VERSION: return "VERSION";
        case TOKEN_ALT: return "ALT";
        case TOKEN_LAYOUTS: return "LAYOUTS";
        case TOKEN_PORT: return "PORT";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_OPEN_BRACE: return "OPEN_BRACE";
        case TOKEN_CLOSE_BRACE: return "CLOSE_BRACE";
        case TOKEN_OPEN_PAREN: return "OPEN_PAREN";
        case TOKEN_CLOSE_PAREN: return "CLOSE_PAREN";
        case TOKEN_EOF: return "EOF";
        case TOKEN_UNKNOWN: return "UNKNOWN";
    }
    return "INVALID";
}

void initLexer(Lexer *lexer, const char *source, struct Parser *parser) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->parser = parser;
}

Token getNextToken(Lexer *lexer) {
    skipWhitespace(lexer);
    lexer->start = lexer->current;

    if (isAtEnd(lexer)) {
        return makeToken(lexer, TOKEN_EOF);
    }

    char c = advance(lexer);

    Token token;
    switch (c) {
        case '{': token = makeToken(lexer, TOKEN_OPEN_BRACE); break;
        case '}': token = makeToken(lexer, TOKEN_CLOSE_BRACE); break;
        case '(': token = makeToken(lexer, TOKEN_OPEN_PAREN); break;
        case ')': token = makeToken(lexer, TOKEN_CLOSE_PAREN); break;
        case '"': token = stringLiteral(lexer); break;
        default:
            if (isAlpha(c)) {
                token = identifierOrKeyword(lexer);
            } else if (isDigit(c)) {
                // Handle numbers
                while (isDigit(peek(lexer))) advance(lexer);
                token = makeToken(lexer, TOKEN_NUMBER);
            } else {
                token = errorToken(lexer->parser, "Unexpected character.", lexer->line);
            }
            break;
    }

    return token;
}
