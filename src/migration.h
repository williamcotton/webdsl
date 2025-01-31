#ifndef MIGRATION_H
#define MIGRATION_H

// Run a migration command
// Returns 0 on success, non-zero on error
int runMigration(const char* cmd, const char* webdsl_path, const char* name);

#endif // MIGRATION_H
