#include "db.h"
#include "stringbuilder.h"
#include <stdio.h>
#include <string.h>

Database* initDatabase(Arena *arena, const char *conninfo) {
    if (!arena || !conninfo) {
        fputs("Database arena or connection info cannot be NULL\n", stderr);
        return NULL;
    }

    Database *db = arenaAlloc(arena, sizeof(Database));
    if (!db) {
        fputs("Failed to allocate database connection\n", stderr);
        return NULL;
    }
    
    db->conninfo = arenaDupString(arena, conninfo);
    db->conn = PQconnectdb(conninfo);
    
    if (PQstatus(db->conn) != CONNECTION_OK) {
        fprintf(stderr, "Database connection failed: %s", 
                PQerrorMessage(db->conn));
        PQfinish(db->conn);
        return NULL;
    }
    
    return db;
}

PGresult* executeQuery(Database *db, const char *query) {
    if (!db || !db->conn) {
        fputs("Invalid database connection\n", stderr);
        return NULL;
    }

    if (!query) {
        fputs("Query cannot be NULL\n", stderr);
        return NULL;
    }
    
    PGresult *result = PQexec(db->conn, query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK && 
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(db->conn));
        PQclear(result);
        return NULL;
    }
    
    return result;
}

void freeResult(PGresult *result) {
    if (result) {
        PQclear(result);
    }
}

void closeDatabase(Database *db) {
    if (db && db->conn) {
        PQfinish(db->conn);
        db->conn = NULL;
    }
}

const char* getDatabaseError(Database *db) {
    return db && db->conn ? PQerrorMessage(db->conn) : "No database connection";
}

static void appendJsonString(StringBuilder *sb, const char *str) {
    StringBuilder_append(sb, "\"");
    
    // Escape special characters
    while (*str) {
        switch (*str) {
            case '"':  StringBuilder_append(sb, "\\\""); break;
            case '\\': StringBuilder_append(sb, "\\\\"); break;
            case '\b': StringBuilder_append(sb, "\\b");  break;
            case '\f': StringBuilder_append(sb, "\\f");  break;
            case '\n': StringBuilder_append(sb, "\\n");  break;
            case '\r': StringBuilder_append(sb, "\\r");  break;
            case '\t': StringBuilder_append(sb, "\\t");  break;
            default:   StringBuilder_append(sb, "%c", *str);
        }
        str++;
    }
    
    StringBuilder_append(sb, "\"");
}

char* resultToJson(Arena *arena, PGresult *result) {
    if (!arena || !result) return NULL;
    
    StringBuilder *sb = StringBuilder_new(arena);
    StringBuilder_append(sb, "{\n  \"rows\": [\n");
    
    int rows = PQntuples(result);
    int cols = PQnfields(result);
    
    // For each row
    for (int i = 0; i < rows; i++) {
        StringBuilder_append(sb, "    {");
        
        // For each column
        for (int j = 0; j < cols; j++) {
            // Add column name
            appendJsonString(sb, PQfname(result, j));
            StringBuilder_append(sb, ": ");
            
            // Add value
            if (PQgetisnull(result, i, j)) {
                StringBuilder_append(sb, "null");
            } else {
                appendJsonString(sb, PQgetvalue(result, i, j));
            }
            
            if (j < cols - 1) {
                StringBuilder_append(sb, ", ");
            }
        }
        
        StringBuilder_append(sb, "}%s\n", i < rows - 1 ? "," : "");
    }
    
    StringBuilder_append(sb, "  ]\n}");
    
    return arenaDupString(arena, StringBuilder_get(sb));
}
