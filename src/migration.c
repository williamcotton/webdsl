#include "migration.h"
#include "server/db.h"
#include "parser.h"
#include "website.h"
#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include "../deps/dotenv-c/dotenv.h"

// Internal functions
static bool createMigrationDir(void);
static bool readMigrationFile(const char* path, char** content, size_t* size);
static bool writeMigrationFile(const char* path, const char* content);
static char* generateTimestamp(void);
static bool ensureMigrationsTable(Database* db);

// Parse website without freeing the arena
static WebsiteNode* parseWebsiteForMigration(Parser* parser, const char* filename) {
    char* source = readFile(filename);
    if (source == NULL) {
        fprintf(stderr, "Could not read file %s\n", filename);
        return NULL;
    }

    initParser(parser, source);
    WebsiteNode* website = parseProgram(parser);
    free(source);

    if (website != NULL && !parser->hadError) {
        return website;
    } else {
        fputs("Parsing failed\n", stderr);
        return NULL;
    }
}

// Get database connection from webdsl file
static Database* getDatabase(const char* webdsl_path, Arena* arena) {
    Parser parser = {0};
    parser.arena = arena;

    printf("webdsl_path: %s\n", webdsl_path);
    
    WebsiteNode* website = parseWebsiteForMigration(&parser, webdsl_path);
    if (parser.hadError || !website) {
        fprintf(stderr, "Error: Could not parse website configuration\n");
        return NULL;
    }
    
    if (website->databaseUrl.type == VALUE_NULL) {
        fprintf(stderr, "Error: Website configuration must include database connection\n");
        return NULL;
    }
    
    char* dbUrl = resolveString(arena, &website->databaseUrl);
    if (!dbUrl) {
        fprintf(stderr, "Error: Could not resolve database URL\n");
        return NULL;
    }
    
    Database* db = initDatabase(arena, dbUrl);
    if (!db) {
        fprintf(stderr, "Error: Could not connect to database\n");
        return NULL;
    }
    
    return db;
}

static bool createMigrationDir(void) {
    char* dir = strdup("migrations");
    if (!dir) {
        fprintf(stderr, "Error: Out of memory\n");
        return false;
    }
    
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Could not create migrations directory: %s\n", strerror(errno));
        free(dir);
        return false;
    }
    
    free(dir);
    return true;
}

static char* generateTimestamp(void) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    
    const size_t size = 15; // YYYYMMDDHHMMSS + \0
    char* timestamp = malloc(size);
    if (!timestamp) {
        return NULL;
    }
    
    snprintf(timestamp, size, "%04d%02d%02d%02d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    
    return timestamp;
}

static bool ensureMigrationsTable(Database* db) {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS migrations ("
        "    id SERIAL PRIMARY KEY,"
        "    name VARCHAR(255) NOT NULL UNIQUE,"
        "    checksum VARCHAR(64) NOT NULL,"
        "    applied_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,"
        "    applied_by VARCHAR(255)"
        ")";
    
    PGresult* result = executeQuery(db, sql);
    if (!result) {
        fprintf(stderr, "Error: Could not create migrations table\n");
        return false;
    }
    PQclear(result);
    return true;
}

static bool readMigrationFile(const char* path, char** content, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open %s: %s\n", path, strerror(errno));
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size < 0) {
        fprintf(stderr, "Error: Could not get file size for %s\n", path);
        fclose(f);
        return false;
    }
    *size = (size_t)file_size;
    fseek(f, 0, SEEK_SET);
    
    *content = malloc(*size + 1);
    if (!*content) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return false;
    }
    
    if (fread(*content, 1, *size, f) != *size) {
        fprintf(stderr, "Error: Could not read %s: %s\n", path, strerror(errno));
        free(*content);
        fclose(f);
        return false;
    }
    
    (*content)[*size] = '\0';
    fclose(f);
    return true;
}

static bool writeMigrationFile(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: Could not create %s: %s\n", path, strerror(errno));
        return false;
    }
    
    size_t len = strlen(content);
    if (fwrite(content, 1, len, f) != len) {
        fprintf(stderr, "Error: Could not write to %s: %s\n", path, strerror(errno));
        fclose(f);
        return false;
    }
    
    fclose(f);
    return true;
}

// Command implementations
static int migrateUp(Database* db) {
    if (!ensureMigrationsTable(db)) {
        return 1;
    }
    
    // Get list of applied migrations
    PGresult* result = executeQuery(db, "SELECT name FROM migrations ORDER BY id");
    if (!result) {
        fprintf(stderr, "Error: Could not get applied migrations\n");
        return 1;
    }
    
    // Build hash table of applied migrations for O(1) lookup
    bool applied[1000] = {false};  // Assuming max 1000 migrations
    int num_applied = PQntuples(result);
    for (int i = 0; i < num_applied; i++) {
        const char* name = PQgetvalue(result, i, 0);
        // Hash the name to an index (simple hash for now)
        size_t hash = 0;
        for (const char* p = name; *p; p++) {
            hash = hash * 31 + (unsigned char)*p;  // Cast to unsigned char first
        }
        applied[hash % 1000] = true;
    }
    PQclear(result);
    
    // Read migrations directory
    DIR* dir = opendir("migrations");
    if (!dir) {
        fprintf(stderr, "Error: Could not open migrations directory\n");
        return 1;
    }
    
    // Get list of migration directories
    struct dirent* entry;
    char* migrations[1000];  // Assuming max 1000 migrations
    int num_migrations = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            migrations[num_migrations++] = strdup(entry->d_name);
        }
    }
    closedir(dir);
    
    // Sort migrations by name (timestamp_name format ensures correct order)
    for (int i = 0; i < num_migrations - 1; i++) {
        for (int j = i + 1; j < num_migrations; j++) {
            if (strcmp(migrations[i], migrations[j]) > 0) {
                char* temp = migrations[i];
                migrations[i] = migrations[j];
                migrations[j] = temp;
            }
        }
    }
    
    // Apply pending migrations
    bool success = true;
    for (int i = 0; i < num_migrations; i++) {
        // Check if migration is already applied
        size_t hash = 0;
        for (const char* p = migrations[i]; *p; p++) {
            hash = hash * 31 + (unsigned char)*p;  // Cast to unsigned char first
        }
        if (!applied[hash % 1000]) {
            // Read up.sql
            char path[1024];
            snprintf(path, sizeof(path), "migrations/%s/up.sql", migrations[i]);
            
            char* content;
            size_t size;
            if (!readMigrationFile(path, &content, &size)) {
                success = false;
                break;
            }
            
            // Execute migration
            printf("Applying migration %s...\n", migrations[i]);
            PGresult* res = executeQuery(db, content);
            free(content);
            
            if (!res) {
                fprintf(stderr, "Error: Migration failed\n");
                success = false;
                break;
            }
            PQclear(res);
            
            // Record migration
            char insert_sql[1024];
            snprintf(insert_sql, sizeof(insert_sql),
                    "INSERT INTO migrations (name, checksum) VALUES ('%s', '')",
                    migrations[i]);
            
            res = executeQuery(db, insert_sql);
            if (!res) {
                fprintf(stderr, "Error: Could not record migration\n");
                success = false;
                break;
            }
            PQclear(res);
            
            printf("Migration %s applied successfully\n", migrations[i]);
        }
    }
    
    // Free migration names
    for (int i = 0; i < num_migrations; i++) {
        free(migrations[i]);
    }
    
    return success ? 0 : 1;
}

static int migrateDown(Database* db) {
    if (!ensureMigrationsTable(db)) {
        return 1;
    }
    
    // Get the most recently applied migration
    PGresult* result = executeQuery(db, "SELECT name FROM migrations ORDER BY id DESC LIMIT 1");
    if (!result) {
        fprintf(stderr, "Error: Could not get last migration\n");
        return 1;
    }
    
    if (PQntuples(result) == 0) {
        fprintf(stderr, "No migrations to revert\n");
        PQclear(result);
        return 0;
    }
    
    const char* migration_name = PQgetvalue(result, 0, 0);
    printf("Reverting migration %s...\n", migration_name);
    
    // Read down.sql
    char path[1024];
    snprintf(path, sizeof(path), "migrations/%s/down.sql", migration_name);
    
    char* content;
    size_t size;
    if (!readMigrationFile(path, &content, &size)) {
        PQclear(result);
        return 1;
    }
    
    // Execute down migration
    PGresult* down_result = executeQuery(db, content);
    free(content);
    
    if (!down_result) {
        fprintf(stderr, "Error: Migration revert failed\n");
        PQclear(result);
        return 1;
    }
    PQclear(down_result);
    
    // Remove migration record
    char delete_sql[1024];
    snprintf(delete_sql, sizeof(delete_sql),
            "DELETE FROM migrations WHERE name = '%s'",
            migration_name);
    
    PGresult* delete_result = executeQuery(db, delete_sql);
    if (!delete_result) {
        fprintf(stderr, "Error: Could not remove migration record\n");
        PQclear(result);
        return 1;
    }
    PQclear(delete_result);
    
    printf("Migration %s reverted successfully\n", migration_name);
    PQclear(result);
    return 0;
}

static int migrateStatus(Database* db) {
    if (!ensureMigrationsTable(db)) {
        return 1;
    }
    
    // Get list of applied migrations
    PGresult* result = executeQuery(db, "SELECT name, applied_at FROM migrations ORDER BY id");
    if (!result) {
        fprintf(stderr, "Error: Could not get applied migrations\n");
        return 1;
    }
    
    // Build hash table of applied migrations with timestamps
    int num_applied = PQntuples(result);
    char* applied[1000] = {NULL};  // Store names of applied migrations
    char* timestamps[1000] = {NULL};  // Store timestamps
    
    for (int i = 0; i < num_applied && i < 1000; i++) {
        applied[i] = strdup(PQgetvalue(result, i, 0));
        timestamps[i] = strdup(PQgetvalue(result, i, 1));
    }
    PQclear(result);
    
    // Read migrations directory
    DIR* dir = opendir("migrations");
    if (!dir) {
        fprintf(stderr, "Error: Could not open migrations directory\n");
        for (int i = 0; i < num_applied; i++) {
            free(applied[i]);
            free(timestamps[i]);
        }
        return 1;
    }
    
    // Get list of all migrations
    struct dirent* entry;
    char* migrations[1000];  // Assuming max 1000 migrations
    int num_migrations = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            migrations[num_migrations++] = strdup(entry->d_name);
        }
    }
    closedir(dir);
    
    // Sort migrations by name
    for (int i = 0; i < num_migrations - 1; i++) {
        for (int j = i + 1; j < num_migrations; j++) {
            if (strcmp(migrations[i], migrations[j]) > 0) {
                char* temp = migrations[i];
                migrations[i] = migrations[j];
                migrations[j] = temp;
            }
        }
    }
    
    // Print status
    printf("\nMigration Status:\n");
    printf("=================\n\n");
    
    for (int i = 0; i < num_migrations; i++) {
        bool is_applied = false;
        const char* timestamp = NULL;
        
        // Check if migration is applied
        for (int j = 0; j < num_applied; j++) {
            if (applied[j] && strcmp(migrations[i], applied[j]) == 0) {
                is_applied = true;
                timestamp = timestamps[j];
                break;
            }
        }
        
        if (is_applied) {
            printf("✓ %s (applied at %s)\n", migrations[i], timestamp);
        } else {
            printf("✗ %s (pending)\n", migrations[i]);
        }
    }
    
    printf("\nTotal: %d migrations (%d applied, %d pending)\n",
           num_migrations, num_applied, num_migrations - num_applied);
    
    // Cleanup
    for (int i = 0; i < num_migrations; i++) {
        free(migrations[i]);
    }
    for (int i = 0; i < num_applied; i++) {
        free(applied[i]);
        free(timestamps[i]);
    }
    
    return 0;
}

static int migrateCreate(Database* db, const char* name) {
    (void)db; // Unused parameter
    
    if (!createMigrationDir()) {
        return 1;
    }
    
    char* timestamp = generateTimestamp();
    if (!timestamp) {
        fprintf(stderr, "Error: Could not generate timestamp\n");
        return 1;
    }
    
    // Create migration directory
    char* dirname = malloc(strlen(timestamp) + strlen(name) + 2);
    if (!dirname) {
        fprintf(stderr, "Error: Out of memory\n");
        free(timestamp);
        return 1;
    }
    
    const size_t dirname_size = strlen(timestamp) + strlen(name) + 2;
    snprintf(dirname, dirname_size, "%s_%s", timestamp, name);
    
    const size_t path_size = strlen("migrations/") + strlen(dirname) + 1;
    char* path = malloc(path_size);
    if (!path) {
        fprintf(stderr, "Error: Out of memory\n");
        free(timestamp);
        free(dirname);
        return 1;
    }
    snprintf(path, path_size, "migrations/%s", dirname);
    
    if (mkdir(path, 0755) != 0) {
        fprintf(stderr, "Error: Could not create migration directory: %s\n", strerror(errno));
        free(timestamp);
        free(dirname);
        free(path);
        return 1;
    }
    
    // Create up.sql and down.sql
    const size_t up_path_size = strlen(path) + 8;
    const size_t down_path_size = strlen(path) + 10;
    char* up_path = malloc(up_path_size);
    char* down_path = malloc(down_path_size);
    if (!up_path || !down_path) {
        fprintf(stderr, "Error: Out of memory\n");
        free(timestamp);
        free(dirname);
        free(path);
        free(up_path);
        free(down_path);
        return 1;
    }
    
    snprintf(up_path, up_path_size, "%s/up.sql", path);
    snprintf(down_path, down_path_size, "%s/down.sql", path);
    
    const char* up_template = "-- Write your migration up SQL here\n";
    const char* down_template = "-- Write your migration down SQL here\n";
    
    if (!writeMigrationFile(up_path, up_template) || 
        !writeMigrationFile(down_path, down_template)) {
        free(timestamp);
        free(dirname);
        free(path);
        free(up_path);
        free(down_path);
        return 1;
    }
    
    printf("Created migration %s\n", dirname);
    
    free(timestamp);
    free(dirname);
    free(path);
    free(up_path);
    free(down_path);
    return 0;
}

int runMigration(const char* cmd, const char* webdsl_path, const char* name) {
    env_load(".", true);
    
    Arena* arena = createArena(1024 * 1024); // 1MB initial size
    if (!arena) {
        fprintf(stderr, "Error: Could not create memory arena\n");
        return 1;
    }
    
    Database* db = getDatabase(webdsl_path, arena);
    if (!db) {
        freeArena(arena);
        return 1;
    }
    
    int result;
    if (strcmp(cmd, "up") == 0) {
        result = migrateUp(db);
    } else if (strcmp(cmd, "down") == 0) {
        result = migrateDown(db);
    } else if (strcmp(cmd, "status") == 0) {
        result = migrateStatus(db);
    } else if (strcmp(cmd, "create") == 0) {
        result = migrateCreate(db, name);
    } else {
        fprintf(stderr, "Error: Unknown migration command: %s\n", cmd);
        result = 1;
    }
    
    closeDatabase(db);
    freeArena(arena);
    return result;
}
