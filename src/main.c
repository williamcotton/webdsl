#include "parser.h"
#include "interpreter.h"
#include <stdio.h>

int main(void) {
    // Example DSL input
    const char *sourceCode = "website {\n"
                          "    name \"My Awesome Site\"\n"
                          "    author \"John Smith\"\n"
                          "    version \"1.0\"\n"
                          "\n"
                          "    layouts {\n"
                          "        \"main\" {\n"
                          "            content {\n"
                          "                h1 \"Site Header\"\n"
                          "                p \"Welcome to our website\"\n"
                          "                \"content\"\n"
                          "                p \"Footer text\"\n"
                          "            }\n"
                          "        }\n"
                          "        \"blog\" {\n"
                          "            content {\n"
                          "                h1 \"Blog Layout\"\n"
                          "                \"content\"\n"
                          "                p \"Blog footer\"\n"
                          "            }\n"
                          "        }\n"
                          "    }\n"
                          "\n"
                          "    pages {\n"
                          "        page \"home\" {\n"
                          "            route \"/\"\n"
                          "            layout \"main\"\n"
                          "            content {\n"
                          "                h1 \"Welcome!\"\n"
                          "                p {\n"
                          "                    link \"/about\" \"Learn more about our site\"\n"
                          "                }\n"
                          "                p \"This is a regular paragraph.\"\n"
                          "            }\n"
                          "        }\n"
                          "        page \"blog\" {\n"
                          "            route \"/blog\"\n"
                          "            layout \"blog\"\n"
                          "            content {\n"
                          "                h1 \"Latest Posts\"\n"
                          "                p \"Check out our latest blog posts!\"\n"
                          "            }\n"
                          "        }\n"
                          "    }\n"
                          "\n"
                          "    styles {\n"
                          "        body {\n"
                          "            background \"#ffffff\"\n"
                          "            color \"#333\"\n"
                          "        }\n"
                          "        h1 {\n"
                          "            color \"#ff6600\"\n"
                          "        }\n"
                          "    }\n"
                          "}\n";

    Parser parser;
    initParser(&parser, sourceCode);

    WebsiteNode *website = parseProgram(&parser);

    if (!parser.hadError) {
        interpretWebsite(website);
    } else {
        fputs("\nParsing failed due to errors.\n", stderr);
    }

    // Free all memory at once
    freeArena(parser.arena);

    return parser.hadError ? 1 : 0;
}
