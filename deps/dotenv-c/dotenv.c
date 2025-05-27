#include <ctype.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "dotenv.h"
#ifdef linux
#include <bsd/string.h>
#endif

// Helper function to skip leading whitespace
static const char *skip_whitespace(const char *str) {
  while (*str && isspace(*str))
    str++;
  return str;
}

static char *concat(char *buffer, char *string) {
  if (!buffer) {
    return strdup(string);
  }
  if (string) {
    size_t length = strlen(buffer) + strlen(string) + 1;
    char *new = realloc(buffer, length);
    strlcat(new, string, length);
    return new;
  }
  return buffer;
}

static bool is_nested(char *value) {
  return strstr(value, "${") && strstr(value, "}");
}

static char *prepare_value(char *value) {
  char *new = malloc(strlen(value) + 2);
  snprintf(new, strlen(value) + 2, "%s", value);
  return new;
}

static char *parse_value(char *value) {
  if (!value)
    return NULL;

  value = prepare_value(value);
  char *search = value;
  char *parsed = NULL;
  char *tok_ptr;
  char *name;

  if (is_nested(value)) {
    while (1) {
      parsed = concat(parsed, strtok_r(search, "${", &tok_ptr));
      name = strtok_r(NULL, "}", &tok_ptr);
      if (!name)
        break;

      char *env_value = getenv(name);
      if (env_value)
        parsed = concat(parsed, env_value);

      search = NULL;
    }
    free(value);
    return parsed;
  }
  return value;
}

static bool is_commented(const char *line) {
  if (!line)
    return false;
  const char *trimmed = skip_whitespace(line);
  return *trimmed == '#';
}

static void set_variable(char *name, char *original, bool overwrite) {
  if (!name)
    return;
  const char *trimmed_name = skip_whitespace(name);
  char *name_copy = strdup(trimmed_name);

  char *parsed = NULL;
  if (original) {
    const char *trimmed_original = skip_whitespace(original);
    char *original_copy = strdup(trimmed_original);
    parsed = parse_value(original_copy);
    if (parsed) {
      setenv(name_copy, parsed, overwrite);
      free(parsed);
    }
    free(original_copy);
  }
  free(name_copy);
}

static void parse(FILE *file, bool overwrite) {
  char *line = NULL;
  size_t len = 0;

  while (getline(&line, &len, file) != -1) {
    if (!is_commented(line)) {
      char *tok_ptr;
      char *name = strtok_r(line, "=", &tok_ptr);
      char *original = strtok_r(NULL, "\n", &tok_ptr);
      set_variable(name, original, overwrite);
    }
  }
  free(line);
}

int env_load(const char *base_path, bool overwrite) {
  if (!base_path)
    return -1;

  size_t path_len = strlen(base_path) + strlen("/.env") + 1;
  char *path = malloc(path_len);
  snprintf(path, path_len, "%s/.env", base_path);

  FILE *file = fopen(path, "r");
  if (!file) {
    free(path);
    return -1;
  }

  parse(file, overwrite);
  fclose(file);
  free(path);
  return 0;
}
