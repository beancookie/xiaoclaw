#include "skills/skill_loader.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include "esp_log.h"

static const char *TAG = "skills";

#define MAX_SKILLS 16

/* Cached skills list */
static skill_info_t s_skills[MAX_SKILLS];
static int s_skill_count = 0;

/* ─── Helper: trim whitespace ──────────────────────────────────────────── */

static void trimWhitespace(const char *start, size_t len, char *out, size_t out_size)
{
    /* Find first non-whitespace */
    const char *p = start;
    size_t trimmed = len;

    while (trimmed > 0 && isspace((unsigned char)p[trimmed - 1])) {
        trimmed--;
    }

    size_t copy = trimmed < out_size - 1 ? trimmed : out_size - 1;
    memcpy(out, p, copy);
    out[copy] = '\0';
}

/* ─── Helper: parse simple YAML frontmatter ────────────────────────────── */

typedef struct {
    char name[64];
    char description[128];
    bool always;
} skill_meta_t;

static void init_meta(skill_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->always = false;
}

static void parse_frontmatter(const char *content, skill_meta_t *meta)
{
    init_meta(meta);

    /* Must start with --- */
    if (strncmp(content, "---", 3) != 0) {
        return;
    }

    const char *end = strstr(content + 3, "---");
    if (!end) return;

    /* Parse lines between --- markers */
    const char *line = content + 3;
    const char *line_end;

    while (line < end) {
        /* Find end of line */
        line_end = line;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        size_t line_len = line_end - line;
        if (line_len > 0) {
            /* Skip leading whitespace */
            while (line_len > 0 && isspace((unsigned char)*line)) {
                line++;
                line_len--;
            }

            if (line_len > 0 && *line != '#') {
                /* Look for "key: value" or "key:" pattern */
                const char *colon = line;
                while (colon < line + line_len && *colon != ':') {
                    colon++;
                }

                if (colon < line + line_len) {
                    char key[32];
                    char value[128];

                    trimWhitespace(line, colon - line, key, sizeof(key));
                    trimWhitespace(colon + 1, line_len - (colon - line + 1), value, sizeof(value));

                    if (strcmp(key, "name") == 0) {
                        strncpy(meta->name, value, sizeof(meta->name) - 1);
                    } else if (strcmp(key, "description") == 0) {
                        strncpy(meta->description, value, sizeof(meta->description) - 1);
                    } else if (strcmp(key, "always") == 0) {
                        meta->always = (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0);
                    }
                }
            }
        }

        /* Move to next line */
        line = line_end;
        while (line < end && (*line == '\n' || *line == '\r')) {
            line++;
        }
    }
}

/* ─── Helper: extract title from markdown ─────────────────────────────── */

static void extract_title(const char *content, char *out, size_t out_size)
{
    /* Skip frontmatter if present */
    const char *start = content;
    if (strncmp(content, "---", 3) == 0) {
        const char *end = strstr(content + 3, "---");
        if (end) {
            start = end + 3;
            while (*start == '\n' || *start == '\r') start++;
        }
    }

    /* Look for # Title */
    while (*start) {
        if (start[0] == '#' && start[1] == ' ') {
            trimWhitespace(start + 2, strlen(start + 2), out, out_size);
            return;
        }
        if (*start == '\n') {
            start++;
        } else {
            break;
        }
    }

    /* Fallback: use first line */
    trimWhitespace(start, strlen(start), out, out_size);
}

/* ─── Helper: extract description from markdown ────────────────────────── */

static void extract_description(const char *content, char *out, size_t out_size)
{
    /* Skip frontmatter and title */
    const char *start = content;

    if (strncmp(content, "---", 3) == 0) {
        const char *end = strstr(content + 3, "---");
        if (end) {
            start = end + 3;
            while (*start == '\n' || *start == '\r') start++;
        }
    }

    /* Skip # Title line */
    while (*start) {
        if (start[0] == '#' && start[1] == ' ') {
            while (*start && *start != '\n') start++;
            while (*start == '\n' || *start == '\r') start++;
            break;
        }
        if (*start == '\n') {
            start++;
        } else {
            break;
        }
    }

    /* Collect text until blank line or section header */
    size_t off = 0;
    while (*start && off < out_size - 1) {
        if (start[0] == '#' && start[1] == '#') {
            break;  /* Section header */
        }
        if (start[0] == '\n') {
            if (off > 0 && out[off - 1] == '\n') {
                break;  /* Blank line */
            }
            out[off++] = ' ';
        } else {
            out[off++] = *start;
        }
        start++;
    }

    /* Trim trailing whitespace */
    while (off > 0 && isspace((unsigned char)out[off - 1])) {
        off--;
    }
    out[off] = '\0';
}

/* ─── Helper: scan skills directory ────────────────────────────────────── */

static void scan_skills_dir(const char *base_path, char source)
{
    DIR *dir = opendir(base_path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_skill_count < MAX_SKILLS) {
        /* Skip . and .. */
        if (ent->d_name[0] == '.') continue;

        /* Check if it's a directory */
        char skill_path[296];
        snprintf(skill_path, sizeof(skill_path), "%s/%s", base_path, ent->d_name);

        DIR *skill_dir = opendir(skill_path);
        if (!skill_dir) continue;

        /* Look for SKILL.md */
        char skill_file[320];
        snprintf(skill_file, sizeof(skill_file), "%s/SKILL.md", skill_path);

        FILE *f = fopen(skill_file, "r");
        if (!f) {
            closedir(skill_dir);
            continue;
        }

        /* Read content for parsing */
        char content[4096];
        size_t n = fread(content, 1, sizeof(content) - 1, f);
        content[n] = '\0';
        fclose(f);

        /* Parse frontmatter */
        skill_meta_t meta;
        parse_frontmatter(content, &meta);

        /* Extract title if not in frontmatter */
        char title[64];
        if (meta.name[0]) {
            strncpy(title, meta.name, sizeof(title) - 1);
        } else {
            extract_title(content, title, sizeof(title));
            strncpy(meta.name, ent->d_name, sizeof(meta.name) - 1);
        }

        /* Extract description if not in frontmatter */
        if (!meta.description[0]) {
            extract_description(content, meta.description, sizeof(meta.description));
        }

        /* Build skill info */
        skill_info_t *info = &s_skills[s_skill_count];
        memset(info, 0, sizeof(*info));

        strncpy(info->name, meta.name, sizeof(info->name) - 1);
        strncpy(info->description, meta.description, sizeof(info->description) - 1);
        info->always = meta.always;
        info->available = true;  /* TODO: check requirements */
        strncpy(info->path, skill_file, sizeof(info->path) - 1);
        info->source = source;

        ESP_LOGD(TAG, "Found skill: %s (always=%d, desc=%s)",
                 info->name, info->always, info->description);

        s_skill_count++;
        closedir(skill_dir);
    }

    closedir(dir);
}

/* ─── Public API ────────────────────────────────────────────────────────── */

esp_err_t skill_loader_init(void)
{
    ESP_LOGI(TAG, "Initializing skills system");

    s_skill_count = 0;

    /* Scan workspace skills */
    char workspace_path[256];
    snprintf(workspace_path, sizeof(workspace_path), "%s/skills", MIMI_SPIFFS_BASE);
    scan_skills_dir(workspace_path, 'w');

    ESP_LOGI(TAG, "Skills system ready (%d skills)", s_skill_count);
    return ESP_OK;
}

size_t skill_loader_build_summary(char *buf, size_t size)
{
    if (s_skill_count == 0) {
        buf[0] = '\0';
        return 0;
    }

    size_t off = 0;
    off += snprintf(buf + off, size - off, "<skills>\n");

    for (int i = 0; i < s_skill_count && off < size - 1; i++) {
        skill_info_t *s = &s_skills[i];

        off += snprintf(buf + off, size - off,
            "  <skill available=\"%s\">\n"
            "    <name>%s</name>\n"
            "    <description>%s</description>\n"
            "    <location>%s</location>\n"
            "  </skill>\n",
            s->available ? "true" : "false",
            s->name,
            s->description,
            s->path);
    }

    off += snprintf(buf + off, size - off, "</skills>\n");

    return off;
}

int skill_loader_list(skill_info_t *skills, int max)
{
    int count = s_skill_count < max ? s_skill_count : max;
    for (int i = 0; i < count; i++) {
        skills[i] = s_skills[i];
    }
    return s_skill_count;
}

esp_err_t skill_loader_load(const char *name, char *buf, size_t size)
{
    for (int i = 0; i < s_skill_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            FILE *f = fopen(s_skills[i].path, "r");
            if (!f) {
                return ESP_ERR_NOT_FOUND;
            }

            size_t n = fread(buf, 1, size - 1, f);
            buf[n] = '\0';
            fclose(f);

            /* Strip frontmatter for content */
            char *content = buf;
            if (strncmp(content, "---", 3) == 0) {
                char *end = strstr(content + 3, "---");
                if (end) {
                    content = end + 3;
                    while (*content == '\n' || *content == '\r') content++;
                }
            }

            if (content != buf) {
                memmove(buf, content, strlen(content) + 1);
            }

            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

size_t skill_loader_get_always_content(char *buf, size_t size)
{
    size_t off = 0;

    for (int i = 0; i < s_skill_count && off < size - 1; i++) {
        if (s_skills[i].always && s_skills[i].available) {
            /* Load skill content */
            char content[4096];
            esp_err_t err = skill_loader_load(s_skills[i].name, content, sizeof(content));
            if (err != ESP_OK) continue;

            /* Strip frontmatter */
            char *start = content;
            if (strncmp(start, "---", 3) == 0) {
                char *end = strstr(start + 3, "---");
                if (end) {
                    start = end + 3;
                    while (*start == '\n' || *start == '\r') start++;
                }
            }

            if (off > 0 && off < size - 1) {
                off += snprintf(buf + off, size - off, "\n---\n\n");
            }

            size_t len = strlen(start);
            size_t copy = len < size - off - 1 ? len : size - off - 1;
            memcpy(buf + off, start, copy);
            off += copy;
        }
    }

    buf[off] = '\0';
    return off;
}

bool skill_loader_check_requirements(const char *name)
{
    /* TODO: Implement requirement checking */
    (void)name;
    return true;
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
    /* Skip leading "- " prefix if present */
    if (line[0] == '-' && line[1] == ' ') {
        line += 2;
    }

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
