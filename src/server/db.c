#include "db.h"
#include "db_pool.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <uthash.h>
#include <stdatomic.h>
#include "server/utils.h"
#include "routing.h"

extern Database *globalDb;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
// Generate unique statement names
static atomic_ulong stmt_counter = 0;

static PGconn *getDbConnection(Database *db) {
  if (!db || !db->pool)
    return NULL;

  PooledConnection *conn = getConnection(db->pool);
  return conn ? conn->conn : NULL;
}

static PreparedStmt* findPreparedStmt(Database *db, PGconn *conn, const char *sql) {
    uint32_t hash = hashString(sql) & STMT_HASH_MASK;
    PreparedStmt *stmt = db->stmt_cache[hash];
    
    while (stmt) {
        if (stmt->conn == conn && strcmp(stmt->sql, sql) == 0) {
            return stmt;
        }
        stmt = stmt->next;
    }
    
    return NULL;
}

static PreparedStmt* prepareSqlStatement(Database *db, PGconn *conn, const char *sql) {
    PreparedStmt *stmt = NULL;
    
    pthread_mutex_lock(&db->stmt_lock);
    
    // Check if already prepared on this connection
    stmt = findPreparedStmt(db, conn, sql);
    if (stmt) {
        pthread_mutex_unlock(&db->stmt_lock);
        return stmt;
    }
    
    // Generate unique name for this statement
    char buf[32];
    snprintf(buf, sizeof(buf), "stmt_%d_%lu", PQbackendPID(conn), atomic_fetch_add(&stmt_counter, 1));
    const char *stmt_name = arenaDupString(db->pool->arena, buf);
    
    // Prepare the statement
    PGresult *res = PQprepare(conn, stmt_name, sql, 0, NULL);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", PQerrorMessage(conn));
        PQclear(res);
        pthread_mutex_unlock(&db->stmt_lock);
        return NULL;
    }
    PQclear(res);
    
    // Add to cache
    uint32_t hash = hashString(sql) & STMT_HASH_MASK;
    stmt = arenaAlloc(db->pool->arena, sizeof(PreparedStmt));
    stmt->sql = arenaDupString(db->pool->arena, sql);
    stmt->name = stmt_name;
    stmt->conn = conn;
    stmt->next = db->stmt_cache[hash];
    db->stmt_cache[hash] = stmt;
    
    pthread_mutex_unlock(&db->stmt_lock);
    return stmt;
}

static void releaseDbConnection(Database *db, PGconn *conn) {
  if (!db || !db->pool || !conn)
    return;

  // Find the PooledConnection that contains this PGconn
  PooledConnection *pooled = db->pool->connections;
  while (pooled) {
    if (pooled->conn == conn) {
      returnConnection(db->pool, pooled);
      break;
    }
    pooled = pooled->next;
  }
}

static PGresult* executePreparedStatement(Database *db, const char *sql,
                                 const char **values, size_t value_count) {
    PGconn *conn = getDbConnection(db);
    if (!conn) return NULL;
    
    // Get or create prepared statement
    PreparedStmt *stmt = prepareSqlStatement(db, conn, sql);
    if (!stmt) {
        releaseDbConnection(db, conn);
        return NULL;
    }
    
    // Execute prepared statement
    PGresult *result = PQexecPrepared(conn, stmt->name, (int)(value_count & INT_MAX), 
                                     values, NULL, NULL, 0);
    
    releaseDbConnection(db, conn);
    return result;
}

// Modify existing executeParameterizedQuery to use prepared statements
static PGresult* executeParameterizedQuery(Database *db, const char *sql, 
                                  const char **values, size_t value_count) {
    return executePreparedStatement(db, sql, values, value_count);
}

Database* initDatabase(Arena *arena, const char *conninfo) {
    if (!arena || !conninfo) {
        fputs("Database arena or connection info cannot be NULL\n", stderr);
        return NULL;
    }

    Database *db = arenaAlloc(arena, sizeof(Database));
    if (!db) {
        fputs("Failed to allocate database structure\n", stderr);
        return NULL;
    }
    
    db->conninfo = arenaDupString(arena, conninfo);
    memset(db->stmt_cache, 0, sizeof(db->stmt_cache));  // Initialize hash table to NULL
    
    if (pthread_mutex_init(&db->stmt_lock, NULL) != 0) {
        return NULL;
    }
    
    db->pool = initConnectionPool(arena, conninfo, INITIAL_POOL_SIZE, MAX_POOL_SIZE);
    if (!db->pool) {
        pthread_mutex_destroy(&db->stmt_lock);
        return NULL;
    }
    printf("Database connection pool initialized\n");
    
    return db;
}

static PGresult* executeQuery(Database *db, const char *query) {
    if (!db || !query) {
        fputs("Invalid database or query\n", stderr);
        return NULL;
    }

    PGconn *conn = getDbConnection(db);
    if (!conn) {
        fputs("Could not get database connection from pool\n", stderr);
        return NULL;
    }

    PGresult *result = PQexec(conn, query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK && 
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
        PQclear(result);
        releaseDbConnection(db, conn);
        return NULL;
    }
    
    releaseDbConnection(db, conn);
    return result;
}

static void freeResult(PGresult *result) {
    if (result) {
        PQclear(result);
    }
}

void closeDatabase(Database *db) {
    if (!db) return;
    
    pthread_mutex_lock(&db->stmt_lock);
    
    // Clear statement cache (no need to free since using arena)
    memset(db->stmt_cache, 0, sizeof(db->stmt_cache));
    
    pthread_mutex_unlock(&db->stmt_lock);
    pthread_mutex_destroy(&db->stmt_lock);
    closeConnectionPool(db->pool);
}

static json_t* resultToJson(PGresult *result) {
    if (!result) return NULL;
    
    // printf("\n=== SQL Result ===\n");
    int rowCount = PQntuples(result);
    int colCount = PQnfields(result);
    
    // // Print column names
    // printf("Columns: ");
    // for (int i = 0; i < colCount; i++) {
    //     printf("%s%s", PQfname(result, i), i < colCount - 1 ? ", " : "\n");
    // }
    
    // // Print first few rows
    // printf("First 3 rows:\n");
    // for (int i = 0; i < rowCount && i < 3; i++) {
    //     printf("Row %d: ", i);
    //     for (int j = 0; j < colCount; j++) {
    //         if (PQgetisnull(result, i, j)) {
    //             printf("NULL%s", j < colCount - 1 ? ", " : "\n");
    //         } else {
    //             printf("%s%s", PQgetvalue(result, i, j), j < colCount - 1 ? ", " : "\n");
    //         }
    //     }
    // }
    // if (rowCount > 3) {
    //     printf("... and %d more rows\n", rowCount - 3);
    // }
    // printf("================\n");
    
    json_t *root = json_object();
    json_t *rows = json_array();
    json_object_set_new(root, "rows", rows);
    
    // For each row
    for (int i = 0; i < rowCount; i++) {
        json_t *row = json_object();
        
        // For each column
        for (int j = 0; j < colCount; j++) {
            const char *colName = PQfname(result, j);
            
            if (PQgetisnull(result, i, j)) {
                json_object_set_new(row, colName, json_null());
            } else {
                const char *value = PQgetvalue(result, i, j);
                json_object_set_new(row, colName, json_string(value));
            }
        }
        
        json_array_append_new(rows, row);
    }
    
    return root;
}

static json_t *executeAndFormatQuery(Arena *arena, char *sql,
                                     const char **values, size_t value_count) {
  (void)arena;

  if (!sql) {
    return NULL;
  }

  // Execute query
  PGresult *result;
  if (values && value_count > 0) {
    result =
        executeParameterizedQuery(globalDb, sql, values, value_count);
  } else {
    result = executeQuery(globalDb, sql);
  }

  if (!result) {
    return NULL;
  }

  // Convert result to JSON
  json_t *jsonData = resultToJson(result);
  freeResult(result);

  if (!jsonData) {
    return NULL;
  }

  return jsonData;
}

json_t *executeSqlStep(PipelineStepNode *step, json_t *input,
                              json_t *requestContext, Arena *arena) {
  (void)requestContext;

  if (step->is_dynamic) {
    // For dynamic SQL, expect input to contain SQL and params
    const char *sql = json_string_value(json_object_get(input, "sql"));
    if (!sql) {
      return NULL;
    }

    json_t *params = json_object_get(input, "params");
    const char **param_values = NULL;
    size_t param_count = 0;

    if (json_is_array(params)) {
      param_count = json_array_size(params);
      if (param_count > 0) {
        param_values = arenaAlloc(arena, sizeof(char *) * param_count);

        for (size_t i = 0; i < param_count; i++) {
          json_t *param = json_array_get(params, i);
          if (json_is_string(param)) {
            param_values[i] = json_string_value(param);
          } else {
            // For non-string values, convert to string
            char *str = json_dumps(param, JSON_COMPACT);
            if (str) {
              param_values[i] = arenaDupString(arena, str);
              free(str);
            }
          }
        }
      }
    }

    PGresult *result =
        executeParameterizedQuery(globalDb, sql, param_values, param_count);
    if (!result) {
      return NULL;
    }

    json_t *jsonResult = resultToJson(result);
    freeResult(result);

    // Merge input properties into jsonResult instead of nesting
    if (input) {
        json_object_update(jsonResult, input);
    }

    if (!jsonResult) {
        fprintf(stderr, "Failed to convert SQL result to JSON\n");
    }
    return jsonResult;
  } else {
    // For static SQL, use the code or look up the query and execute it
    char *sql = step->code;

    if (step->name) {
      QueryNode *query = findQuery(step->name);
      sql = query->sql;
    }

    if (!sql) {
      return NULL;
    }

    // Extract parameters from input if needed
    const char **values = NULL;
    size_t value_count = 0;

    // Check if input has a params array
    if (input) {
      json_t *params = json_object_get(input, "params");
      if (json_is_array(params)) {
        value_count = json_array_size(params);
        if (value_count > 0) {
          values = arenaAlloc(arena, sizeof(char *) * value_count);
          for (size_t i = 0; i < value_count; i++) {
            json_t *param = json_array_get(params, i);
            if (json_is_string(param)) {
              values[i] = json_string_value(param);
            } else if (json_is_number(param)) {
                // For numbers, convert directly to string without extra quotes
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f", json_number_value(param));
                values[i] = arenaDupString(arena, buf);
            } else {
                // For other non-string values, convert to string
                char *str = json_dumps(param, JSON_COMPACT);
                if (str) {
                    values[i] = arenaDupString(arena, str);
                    free(str);
                }
            }
          }
        }
      } else if (json_is_array(input)) {
        // If input itself is an array, use it directly as params
        value_count = json_array_size(input);
        if (value_count > 0) {
          values = arenaAlloc(arena, sizeof(char *) * value_count);
          for (size_t i = 0; i < value_count; i++) {
            json_t *param = json_array_get(input, i);
            if (json_is_string(param)) {
              values[i] = json_string_value(param);
            } else {
              // For non-string values, convert to string
              char *str = json_dumps(param, JSON_COMPACT);
              if (str) {
                values[i] = arenaDupString(arena, str);
                free(str);
              }
            }
          }
        }
      }
    }

    json_t *jsonData = executeAndFormatQuery(arena, sql, values, value_count);
    if (!jsonData) {
      return NULL;
    }

    // Merge input properties into jsonData instead of nesting
    if (input) {
        json_object_update(jsonData, input);
    }

    return jsonData;
  }
}
