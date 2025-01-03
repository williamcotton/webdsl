#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>

static Token errorToken(Parser *parser, const char *message, int line);
static Token rawBlock(Lexer *lexer);

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

static Token rawBlock(Lexer *lexer) {
    // Skip past the opening brace
    advance(lexer);
    
    // Mark the start of content after the brace
    lexer->start = lexer->current;
    
    int braceCount = 1;
    int startLine = lexer->line;
    
    while (!isAtEnd(lexer) && braceCount > 0) {
        if (peek(lexer) == '{') {
            braceCount++;
        } else if (peek(lexer) == '}') {
            braceCount--;
        } else if (peek(lexer) == '\n') {
            lexer->line++;
        }
        
        if (braceCount > 0) {
            advance(lexer);
        }
    }
    
    if (braceCount > 0) {
        return errorToken(lexer->parser, "Unterminated raw block.", startLine);
    }
    
    // Create token before consuming closing brace
    Token token = makeToken(lexer, TOKEN_RAW_BLOCK);
    
    // Consume the closing brace
    advance(lexer);
    
    return token;
}

static TokenType checkKeyword(const char *start, size_t length) {
#define KW_MATCH(kw, ttype) \
    if (strncmp(start, kw, length) == 0 && strlen(kw) == length) \
        return ttype;

    KW_MATCH("website", TOKEN_WEBSITE)
    KW_MATCH("html", TOKEN_HTML)
    KW_MATCH("css", TOKEN_CSS)
    KW_MATCH("pages", TOKEN_PAGES)
    KW_MATCH("database", TOKEN_DATABASE)
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
    KW_MATCH("api", TOKEN_API)
    KW_MATCH("method", TOKEN_METHOD)
    KW_MATCH("jsonResponse", TOKEN_JSON_RESPONSE)
    KW_MATCH("query", TOKEN_QUERY)
    KW_MATCH("sql", TOKEN_SQL)
    KW_MATCH("fields", TOKEN_FIELDS)
    KW_MATCH("jq", TOKEN_JQ)
    KW_MATCH("lua", TOKEN_LUA)
    KW_MATCH("preFilter", TOKEN_PRE_FILTER)
    KW_MATCH("filter", TOKEN_FILTER)
    KW_MATCH("params", TOKEN_PARAMS)

    return TOKEN_UNKNOWN;
#undef KW_MATCH
}

static Token identifierOrKeyword(Lexer *lexer) {
    while (isAlpha(peek(lexer)) || isDigit(peek(lexer)) || 
           peek(lexer) == '_' || peek(lexer) == '-') {
        advance(lexer);
    }

    size_t length = (size_t)(lexer->current - lexer->start);
    
    // Inside brackets, treat everything as a string
    if (lexer->inBrackets) {
        return makeToken(lexer, TOKEN_STRING);
    }
    
    TokenType type = checkKeyword(lexer->start, length);

    // Special handling for raw block keywords
    if (type == TOKEN_HTML || type == TOKEN_SQL || 
        type == TOKEN_JQ || type == TOKEN_LUA) {
        skipWhitespace(lexer);
        if (peek(lexer) == '{') {
            Token token = makeToken(lexer, type);
            lexer->previous = token;  // Store as previous token before processing brace
            return token;
        }
    }
    
    // Special handling for CSS blocks - these go directly to raw block
    if (type == TOKEN_CSS) {
        skipWhitespace(lexer);
        if (peek(lexer) == '{') {
            return rawBlock(lexer);
        }
    }
    
    if (type == TOKEN_UNKNOWN) {
        type = TOKEN_STRING;
    }
    return makeToken(lexer, type);
}

static Token stringLiteral(Lexer *lexer) {
    // First quote already consumed
    if (!isAtEnd(lexer) && 
        peek(lexer) == '"' && 
        peekNext(lexer) == '"') {
        
        // Consume the other two quotes
        advance(lexer);
        advance(lexer);
        
        // Mark start of actual content
        lexer->start = lexer->current;
        
        // Read until we find three closing quotes
        while (!isAtEnd(lexer)) {
            if (!isAtEnd(lexer) && 
                peek(lexer) == '"' && 
                peekNext(lexer) == '"' && 
                lexer->current[2] == '"') {
                break;
            }
            if (peek(lexer) == '\n') {
                lexer->line++;
            }
            advance(lexer);
        }
        
        if (isAtEnd(lexer)) {
            return errorToken(lexer->parser, "Unterminated triple-quoted string.", lexer->line);
        }
        
        // Create token before consuming closing quotes
        Token token = makeToken(lexer, TOKEN_STRING);
        
        // Consume the closing quotes
        advance(lexer);
        advance(lexer);
        advance(lexer);
        
        return token;
    } else {
        // Regular string - mark start after opening quote
        lexer->start = lexer->current;
        
        while (!isAtEnd(lexer) && peek(lexer) != '"') {
            if (peek(lexer) == '\n') {
                lexer->line++;
            }
            if (peek(lexer) == '\\') {
                advance(lexer); // Skip the backslash
                if (!isAtEnd(lexer)) {
                    advance(lexer); // Include the escaped character
                }
                continue;
            }
            advance(lexer);
        }

        if (isAtEnd(lexer)) {
            return errorToken(lexer->parser, "Unterminated string.", lexer->line);
        }

        // Create token before consuming closing quote
        Token token = makeToken(lexer, TOKEN_STRING);
        
        // Consume closing quote
        advance(lexer);
        
        return token;
    }
}

static Token number(Lexer *lexer) {
    while (isDigit(peek(lexer))) advance(lexer);
    
    // Special case for range notation (e.g., "1..100")
    if (peek(lexer) == '.' && peekNext(lexer) == '.') {
        // Include the first number and the ".." in the token
        advance(lexer);  // First dot
        advance(lexer);  // Second dot
        
        // Parse the second number
        if (!isDigit(peek(lexer))) {
            return errorToken(lexer->parser, "Expected number after '..' in range.", lexer->line);
        }
        
        while (isDigit(peek(lexer))) advance(lexer);
        
        return makeToken(lexer, TOKEN_RANGE);
    }
    
    // Regular decimal number handling
    if (peek(lexer) == '.') {
        advance(lexer);  // Consume the '.'
        
        // Must have at least one digit after decimal
        if (!isDigit(peek(lexer))) {
            return errorToken(lexer->parser, "Expected digit after decimal point.", lexer->line);
        }
        
        while (isDigit(peek(lexer))) advance(lexer);
    }
    
    return makeToken(lexer, TOKEN_NUMBER);
}

static Token rawStringLiteral(Lexer *lexer) {
    // Similar to stringLiteral but preserves quotes
    // Used for SQL queries and other raw content
    lexer->start = lexer->current - 1;  // Include opening quote
    
    while (!isAtEnd(lexer) && peek(lexer) != '"') {
        if (peek(lexer) == '\n') lexer->line++;
        if (peek(lexer) == '\\') {
            advance(lexer);
            if (!isAtEnd(lexer)) advance(lexer);
            continue;
        }
        advance(lexer);
    }

    if (isAtEnd(lexer)) {
        return errorToken(lexer->parser, "Unterminated raw string.", lexer->line);
    }

    advance(lexer);  // Closing quote
    return makeToken(lexer, TOKEN_RAW_STRING);
}

const char* getTokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_WEBSITE: return "WEBSITE";
        case TOKEN_HTML: return "HTML";
        case TOKEN_CSS: return "CSS";
        case TOKEN_PAGES: return "PAGES";
        case TOKEN_DATABASE: return "DATABASE";
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
        case TOKEN_API: return "API";
        case TOKEN_METHOD: return "METHOD";
        case TOKEN_JSON_RESPONSE: return "JSON_RESPONSE";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_OPEN_BRACE: return "OPEN_BRACE";
        case TOKEN_CLOSE_BRACE: return "CLOSE_BRACE";
        case TOKEN_OPEN_PAREN: return "OPEN_PAREN";
        case TOKEN_CLOSE_PAREN: return "CLOSE_PAREN";
        case TOKEN_EOF: return "EOF";
        case TOKEN_UNKNOWN: return "UNKNOWN";
        case TOKEN_QUERY: return "QUERY";
        case TOKEN_SQL: return "SQL";
        case TOKEN_RAW_BLOCK: return "RAW_BLOCK";
        case TOKEN_RAW_STRING: return "RAW_STRING";
        case TOKEN_OPEN_BRACKET: return "OPEN_BRACKET";
        case TOKEN_CLOSE_BRACKET: return "CLOSE_BRACKET";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_FIELDS: return "FIELDS";
        case TOKEN_RANGE: return "RANGE";
        case TOKEN_JQ: return "JQ";
        case TOKEN_LUA: return "LUA";
        case TOKEN_PRE_FILTER: return "PRE_FILTER";
        case TOKEN_FILTER: return "FILTER";
        case TOKEN_PARAMS: return "PARAMS";
    }
    return "INVALID";
}

void initLexer(Lexer *lexer, const char *source, struct Parser *parser) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->parser = parser;
    lexer->inBrackets = 0;
}

Token getNextToken(Lexer *lexer) {
    skipWhitespace(lexer);
    lexer->start = lexer->current;

    if (isAtEnd(lexer)) {
        return makeToken(lexer, TOKEN_EOF);
    }

    char c = advance(lexer);

    if (isDigit(c)) {
        return number(lexer);
    }

    Token token;
    switch (c) {
        case '{': 
            // Check if we just returned a HTML, SQL, JQ or LUA token
            if (lexer->previous.type == TOKEN_JQ || 
                lexer->previous.type == TOKEN_HTML ||
                lexer->previous.type == TOKEN_SQL ||
                lexer->previous.type == TOKEN_LUA) {
                // Back up to include the opening brace
                lexer->current--;
                return rawBlock(lexer);
            }
            token = makeToken(lexer, TOKEN_OPEN_BRACE); 
            break;
        case '}': token = makeToken(lexer, TOKEN_CLOSE_BRACE); break;
        case '(': token = makeToken(lexer, TOKEN_OPEN_PAREN); break;
        case ')': token = makeToken(lexer, TOKEN_CLOSE_PAREN); break;
        case '"': token = stringLiteral(lexer); break;
        case '[': 
            lexer->inBrackets = 1;
            token = makeToken(lexer, TOKEN_OPEN_BRACKET); 
            break;
        case ']': 
            lexer->inBrackets = 0;
            token = makeToken(lexer, TOKEN_CLOSE_BRACKET); 
            break;
        case ',': token = makeToken(lexer, TOKEN_COMMA); break;
        default:
            if (isAlpha(c)) {
                token = identifierOrKeyword(lexer);
            } else {
                token = errorToken(lexer->parser, "Unexpected character.", lexer->line);
            }
            break;
    }

    if (token.type == TOKEN_SQL) {
        skipWhitespace(lexer);
        if (peek(lexer) == '"') {
            advance(lexer);
            return rawStringLiteral(lexer);
        }
    }

    // Store this token for next time
    lexer->previous = token;
    return token;
}
