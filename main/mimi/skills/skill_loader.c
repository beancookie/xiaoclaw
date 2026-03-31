#include "skills/skill_loader.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "esp_log.h"

static const char *TAG = "skills";

/*
 * Skills are stored as markdown files in spiffs_data/skills/
 * and flashed into the SPIFFS partition at build time.
 */

esp_err_t skill_loader_init(void)
{
    ESP_LOGI(TAG, "Initializing skills system");

    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SPIFFS — skills may not be available");
        return ESP_OK;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (strncmp(name, "skills/", 7) == 0 && len > 10 &&
            strcmp(name + len - 3, ".md") == 0) {
            count++;
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Skills system ready (%d skills on SPIFFS)", count);
    return ESP_OK;
}

/* ── Build skills summary for system prompt ──────────────────── */

/**
 * Parse first line as title: expects "# Title".
 * Writes the title (without "# " prefix) into out.
 */
static void extract_title(const char *line, size_t len, char *out, size_t out_size)
{
    const char *start = line;
    if (len >= 2 && line[0] == '#' && line[1] == ' ') {
        start = line + 2;
        len -= 2;
    }

    /* Trim trailing whitespace/newline */
    while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' || start[len - 1] == ' ')) {
        len--;
    }

    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, start, copy);
    out[copy] = '\0';
}

/**
 * Extract description: text between the first line and the first blank line.
 */
static void extract_description(FILE *f, char *out, size_t out_size)
{
    size_t off = 0;
    char line[256];

    while (fgets(line, sizeof(line), f) && off < out_size - 1) {
        size_t len = strlen(line);

        /* Stop at blank line or section header */
        if (len == 0 || (len == 1 && line[0] == '\n') ||
            (len >= 2 && line[0] == '#' && line[1] == '#')) {
            break;
        }

        /* Skip leading blank lines */
        if (off == 0 && line[0] == '\n') continue;

        /* Trim trailing newline for concatenation */
        if (line[len - 1] == '\n') {
            line[len - 1] = ' ';
        }

        size_t copy = len < out_size - off - 1 ? len : out_size - off - 1;
        memcpy(out + off, line, copy);
        off += copy;
    }

    /* Trim trailing space */
    while (off > 0 && out[off - 1] == ' ') off--;
    out[off] = '\0';
}

size_t skill_loader_build_summary(char *buf, size_t size)
{
    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SPIFFS for skill enumeration");
        buf[0] = '\0';
        return 0;
    }

    size_t off = 0;
    struct dirent *ent;
    /* SPIFFS readdir returns filenames relative to the mount point (e.g. "skills/weather.md").
       We match entries that start with "skills/" and end with ".md". */
    const char *skills_subdir = "skills/";
    const size_t subdir_len = strlen(skills_subdir);

    while ((ent = readdir(dir)) != NULL && off < size - 1) {
        const char *name = ent->d_name;

        /* Match files under skills/ with .md extension */
        if (strncmp(name, skills_subdir, subdir_len) != 0) continue;

        size_t name_len = strlen(name);
        if (name_len < subdir_len + 4) continue;  /* at least "skills/x.md" */
        if (strcmp(name + name_len - 3, ".md") != 0) continue;

        /* Build full path */
        char full_path[296];
        snprintf(full_path, sizeof(full_path), "%s/%s", MIMI_SPIFFS_BASE, name);

        FILE *f = fopen(full_path, "r");
        if (!f) continue;

        /* Read first line for title */
        char first_line[128];
        if (!fgets(first_line, sizeof(first_line), f)) {
            fclose(f);
            continue;
        }

        char title[64];
        extract_title(first_line, strlen(first_line), title, sizeof(title));

        /* Read description (until blank line) */
        char desc[256];
        extract_description(f, desc, sizeof(desc));
        fclose(f);

        /* Append to summary */
        off += snprintf(buf + off, size - off,
            "- **%s**: %s (read with: read_file %s)\n",
            title, desc, full_path);
    }

    closedir(dir);

    buf[off] = '\0';
    ESP_LOGI(TAG, "Skills summary: %d bytes", (int)off);
    return off;
}

/* ── MCP Server Config ─────────────────────────────────────────── */

static const char *MCP_SERVERS_FILE = MIMI_SPIFFS_BASE "/skills/mcp-servers.md";

/**
 * Trim leading and trailing whitespace
 */
static void trim_string(char *str)
{
    if (!str) return;

    /* Trim leading */
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;

    /* Trim trailing */
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * Parse a key: value or - key: value line
 */
static bool parse_line(const char *line, char *key, char *value)
{
    const char *colon = strchr(line, ':');
    if (!colon) return false;

    size_t key_len = colon - line;
    while (key_len > 0 && (line[key_len - 1] == ' ' || line[key_len - 1] == '\t')) {
        key_len--;
    }

    if (key_len == 0) return false;

    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char *val_start = colon + 1;
    while (*val_start == ' ' || *val_start == '\t') val_start++;

    strcpy(value, val_start);
    trim_string(value);

    return true;
}

esp_err_t skill_loader_get_mcp_server_config(const char *server_name,
                                              char *host, size_t host_size,
                                              int *port, char *endpoint, size_t ep_size)
{
    FILE *f = fopen(MCP_SERVERS_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG, "MCP servers file not found: %s", MCP_SERVERS_FILE);
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    bool in_server_section = false;
    char current_key[64] = {0};
    char current_value[128] = {0};

    /* Build section header to match */
    char target_section[128];
    snprintf(target_section, sizeof(target_section), "## %s", server_name);

    while (fgets(line, sizeof(line), f)) {
        /* Check for server section header */
        if (strncmp(line, target_section, strlen(target_section)) == 0) {
            in_server_section = true;
            continue;
        }

        /* Exit section on next header or EOF */
        if (line[0] == '#' && line[1] == '#') {
            in_server_section = false;
            continue;
        }

        /* Skip non-section lines */
        if (!in_server_section) continue;

        /* Skip empty lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == ' ') continue;

        /* Parse key: value lines */
        if (parse_line(line, current_key, current_value)) {
            if (strcmp(current_key, "host") == 0) {
                strncpy(host, current_value, host_size - 1);
                host[host_size - 1] = '\0';
            } else if (strcmp(current_key, "port") == 0) {
                *port = atoi(current_value);
            } else if (strcmp(current_key, "endpoint") == 0) {
                strncpy(endpoint, current_value, ep_size - 1);
                endpoint[ep_size - 1] = '\0';
            }
        }
    }

    fclose(f);

    /* Check if we found the server */
    if (host[0] == '\0') {
        ESP_LOGW(TAG, "Server '%s' not found in MCP servers file", server_name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Found MCP server config: %s:%d/%s", host, *port, endpoint);
    return ESP_OK;
}
