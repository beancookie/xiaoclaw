#include "skills/skill_meta.h"
#include "mimi_config.h"
#include "util/fatfs_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "skill_meta";
#define SKILL_BUFFER_SIZE 8192

/* Static buffer for parsing - allocated from PSRAM */
static char *s_skill_buffer = NULL;

/* In-memory cache of skill metadata - allocated from PSRAM */
static skill_meta_t *s_skills = NULL;
static int s_skill_count = 0;
static bool s_initialized = false;
static time_t s_last_save = 0;

/* ─── Helper: save metadata to FATFS ─────────────────────────────────── */

static esp_err_t save_to_file(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *skills_arr = cJSON_CreateArray();

    for (int i = 0; i < s_skill_count; i++) {
        cJSON *skill = cJSON_CreateObject();
        cJSON_AddStringToObject(skill, "name", s_skills[i].name);
        cJSON_AddStringToObject(skill, "path", s_skills[i].path);
        cJSON_AddNumberToObject(skill, "usage_count", s_skills[i].usage_count);
        cJSON_AddNumberToObject(skill, "success_count", s_skills[i].success_count);
        cJSON_AddNumberToObject(skill, "success_rate", s_skills[i].success_rate);
        cJSON_AddNumberToObject(skill, "last_used", (double)s_skills[i].last_used);

        /* Extended metadata - only serialize if set */
        if (s_skills[i].description[0] != '\0') {
            cJSON_AddStringToObject(skill, "description", s_skills[i].description);
        }
        if (s_skills[i].one_line_summary[0] != '\0') {
            cJSON_AddStringToObject(skill, "one_line_summary", s_skills[i].one_line_summary);
        }
        if (s_skills[i].category[0] != '\0') {
            cJSON_AddStringToObject(skill, "category", s_skills[i].category);
        }
        if (s_skills[i].tag_count > 0) {
            cJSON *tags_arr = cJSON_CreateArray();
            for (int t = 0; t < s_skills[i].tag_count; t++) {
                cJSON_AddItemToArray(tags_arr, cJSON_CreateString(s_skills[i].tags[t]));
            }
            cJSON_AddItemToObject(skill, "tags", tags_arr);
        }
        if (s_skills[i].tool_count > 0) {
            cJSON *tools_arr = cJSON_CreateArray();
            for (int t = 0; t < s_skills[i].tool_count; t++) {
                cJSON_AddItemToArray(tools_arr, cJSON_CreateString(s_skills[i].tools[t]));
            }
            cJSON_AddItemToObject(skill, "tools", tools_arr);
        }
        /* Quality scores - only serialize if non-zero */
        if (s_skills[i].clarity > 0) {
            cJSON_AddNumberToObject(skill, "clarity", s_skills[i].clarity);
        }
        if (s_skills[i].completeness > 0) {
            cJSON_AddNumberToObject(skill, "completeness", s_skills[i].completeness);
        }
        if (s_skills[i].actionability > 0) {
            cJSON_AddNumberToObject(skill, "actionability", s_skills[i].actionability);
        }

        cJSON_AddItemToArray(skills_arr, skill);
    }

    cJSON_AddItemToObject(root, "skills", skills_arr);
    cJSON_AddNumberToObject(root, "hot_threshold", SKILL_META_HOT_THRESHOLD);
    cJSON_AddNumberToObject(root, "last_updated", (double)time(NULL));

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize skill index JSON");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = fatfs_write_atomic(SKILL_INDEX_PATH, json_str, strlen(json_str));
    free(json_str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to atomically write skill index: %s", SKILL_INDEX_PATH);
        return err;
    }

    s_last_save = time(NULL);
    ESP_LOGI(TAG, "Saved skill index: %d skills", s_skill_count);
    return ESP_OK;
}

/* ─── Helper: load metadata from FATFS ─────────────────────────────── */

static esp_err_t load_from_file(void)
{
    FILE *f = fopen(SKILL_INDEX_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "No existing skill_index.json, starting fresh");
        return ESP_OK;
    }

    /* Read file content into static buffer to avoid stack overflow */
    size_t n = fread(s_skill_buffer, 1, SKILL_BUFFER_SIZE - 1, f);
    s_skill_buffer[n] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(s_skill_buffer);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse skill_index.json, starting fresh");
        return ESP_OK;
    }

    cJSON *skills_arr = cJSON_GetObjectItem(root, "skills");
    if (!cJSON_IsArray(skills_arr)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    s_skill_count = 0;
    cJSON *skill;
    cJSON_ArrayForEach(skill, skills_arr) {
        if (s_skill_count >= SKILL_META_MAX_SKILLS) break;

        skill_meta_t *m = &s_skills[s_skill_count];
        memset(m, 0, sizeof(*m));

        cJSON *name = cJSON_GetObjectItem(skill, "name");
        if (name && name->valuestring) {
            strncpy(m->name, name->valuestring, sizeof(m->name) - 1);
            m->name[sizeof(m->name) - 1] = '\0';
        }

        cJSON *path = cJSON_GetObjectItem(skill, "path");
        if (path && path->valuestring) {
            strncpy(m->path, path->valuestring, sizeof(m->path) - 1);
            m->path[sizeof(m->path) - 1] = '\0';
        }

        cJSON *usage = cJSON_GetObjectItem(skill, "usage_count");
        if (usage) m->usage_count = (int)usage->valueint;

        cJSON *success = cJSON_GetObjectItem(skill, "success_count");
        if (success) m->success_count = (int)success->valueint;

        cJSON *rate = cJSON_GetObjectItem(skill, "success_rate");
        if (rate) m->success_rate = (float)rate->valuedouble;

        cJSON *last_used = cJSON_GetObjectItem(skill, "last_used");
        if (last_used) m->last_used = (time_t)last_used->valueint;

        /* Extended metadata (backward compatible - these fields may not exist in old JSON) */
        cJSON *desc = cJSON_GetObjectItem(skill, "description");
        if (desc && desc->valuestring) {
            strncpy(m->description, desc->valuestring, sizeof(m->description) - 1);
            m->description[sizeof(m->description) - 1] = '\0';
        }

        cJSON *summary = cJSON_GetObjectItem(skill, "one_line_summary");
        if (summary && summary->valuestring) {
            strncpy(m->one_line_summary, summary->valuestring, sizeof(m->one_line_summary) - 1);
            m->one_line_summary[sizeof(m->one_line_summary) - 1] = '\0';
        }

        cJSON *category = cJSON_GetObjectItem(skill, "category");
        if (category && category->valuestring) {
            strncpy(m->category, category->valuestring, sizeof(m->category) - 1);
            m->category[sizeof(m->category) - 1] = '\0';
        }

        cJSON *tags = cJSON_GetObjectItem(skill, "tags");
        if (tags && cJSON_IsArray(tags)) {
            m->tag_count = 0;
            cJSON *tag;
            cJSON_ArrayForEach(tag, tags) {
                if (tag->valuestring && m->tag_count < SKILL_MAX_TAGS) {
                    strncpy(m->tags[m->tag_count], tag->valuestring, sizeof(m->tags[m->tag_count]) - 1);
                    m->tags[m->tag_count][sizeof(m->tags[m->tag_count]) - 1] = '\0';
                    m->tag_count++;
                }
            }
        }

        cJSON *tools = cJSON_GetObjectItem(skill, "tools");
        if (tools && cJSON_IsArray(tools)) {
            m->tool_count = 0;
            cJSON *tool;
            cJSON_ArrayForEach(tool, tools) {
                if (tool->valuestring && m->tool_count < SKILL_MAX_TOOLS) {
                    strncpy(m->tools[m->tool_count], tool->valuestring, sizeof(m->tools[m->tool_count]) - 1);
                    m->tools[m->tool_count][sizeof(m->tools[m->tool_count]) - 1] = '\0';
                    m->tool_count++;
                }
            }
        }

        cJSON *clarity = cJSON_GetObjectItem(skill, "clarity");
        if (clarity) m->clarity = (int)clarity->valueint;

        cJSON *completeness = cJSON_GetObjectItem(skill, "completeness");
        if (completeness) m->completeness = (int)completeness->valueint;

        cJSON *actionability = cJSON_GetObjectItem(skill, "actionability");
        if (actionability) m->actionability = (int)actionability->valueint;

        /* Validate path format: must start with "/" and contain "/fatfs/" */
        if (m->path[0] != '/' || strstr(m->path, "/fatfs/") != m->path) {
            ESP_LOGW(TAG, "Invalid path in skill_index.json, skipping: %s", m->path);
            memset(m, 0, sizeof(*m));
            continue;
        }

        /* Verify the skill file actually exists */
        struct stat st;
        if (stat(m->path, &st) != 0) {
            ESP_LOGW(TAG, "Skill file not found: %s, removing from index", m->path);
            memset(m, 0, sizeof(*m));
            continue;
        }

        s_skill_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d skills from skill_index.json", s_skill_count);
    return ESP_OK;
}

/* ─── Helper: find skill by name ─────────────────────────────────────── */

static skill_meta_t *find_skill(const char *name)
{
    for (int i = 0; i < s_skill_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            return &s_skills[i];
        }
    }
    return NULL;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

esp_err_t skill_meta_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Allocate buffers from PSRAM to free internal SRAM */
    if (!s_skill_buffer) {
        s_skill_buffer = heap_caps_malloc(SKILL_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_skill_buffer) {
            ESP_LOGE(TAG, "Failed to allocate s_skill_buffer from PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_skills) {
        s_skills = heap_caps_calloc(SKILL_META_MAX_SKILLS, sizeof(skill_meta_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_skills) {
            ESP_LOGE(TAG, "Failed to allocate s_skills from PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Initializing skill metadata system");
    s_skill_count = 0;

    esp_err_t err = load_from_file();
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;

    /* Scan auto skills directory to merge any new skills */
    char auto_dir[256];
    snprintf(auto_dir, sizeof(auto_dir), "%s/skills/auto", MIMI_FATFS_BASE);

    DIR *dir = opendir(auto_dir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && s_skill_count < SKILL_META_MAX_SKILLS) {
            if (ent->d_name[0] == '.') continue;

            /* Check if it's a directory (auto skill) */
            char skill_path[1024];
            snprintf(skill_path, sizeof(skill_path), "%s/%s/SKILL.md", auto_dir, ent->d_name);

            FILE *f = fopen(skill_path, "r");
            if (!f) continue;

            /* Check if this skill is already in our index */
            if (!find_skill(ent->d_name)) {
                /* Add new auto skill */
                skill_meta_t *m = &s_skills[s_skill_count];
                memset(m, 0, sizeof(*m));
                strncpy(m->name, ent->d_name, sizeof(m->name) - 1);
                m->name[sizeof(m->name) - 1] = '\0';
                strncpy(m->path, skill_path, sizeof(m->path) - 1);
                m->path[sizeof(m->path) - 1] = '\0';
                m->usage_count = 0;
                m->success_rate = 0.0;
                m->last_used = 0;
                s_skill_count++;
            }
            fclose(f);
        }
        closedir(dir);
    }

    ESP_LOGI(TAG, "Skill meta initialized: %d skills", s_skill_count);
    return ESP_OK;
}

esp_err_t skill_meta_get(const char *name, skill_meta_t *meta)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    skill_meta_t *found = find_skill(name);
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    *meta = *found;
    return ESP_OK;
}

esp_err_t skill_meta_record_usage(const char *name, bool success)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    skill_meta_t *m = find_skill(name);
    if (!m) {
        /* Skill not in index - this shouldn't happen normally,
           but for newly created auto skills, we may need to add them */
        ESP_LOGW(TAG, "Recording usage for unknown skill: %s", name);
        return ESP_ERR_NOT_FOUND;
    }

    m->usage_count++;
    if (success) {
        m->success_count++;
    }
    if (m->usage_count > 0) {
        m->success_rate = (float)m->success_count / (float)m->usage_count;
    }
    m->last_used = time(NULL);

    return save_to_file();
}

esp_err_t skill_meta_update(const char *name, const skill_meta_t *meta)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    skill_meta_t *m = find_skill(name);
    if (!m) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Only allow updating certain fields */
    strncpy(m->path, meta->path, sizeof(m->path) - 1);
    /* Don't reset usage_count via update - use record_usage */

    return save_to_file();
}

esp_err_t skill_meta_add(const skill_meta_t *meta)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (s_skill_count >= SKILL_META_MAX_SKILLS) {
        ESP_LOGE(TAG, "Skill index full (%d)", SKILL_META_MAX_SKILLS);
        return ESP_ERR_NO_MEM;
    }

    /* Check if already exists */
    if (find_skill(meta->name)) {
        ESP_LOGW(TAG, "Skill %s already exists, use update instead", meta->name);
        return ESP_ERR_INVALID_STATE;
    }

    skill_meta_t *m = &s_skills[s_skill_count];
    *m = *meta;
    s_skill_count++;

    return save_to_file();
}

size_t skill_meta_get_all_json(char *buf, size_t size)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    ESP_LOGI(TAG, "skill_meta_get_all_json: returning %d skills", s_skill_count);
    for (int i = 0; i < s_skill_count; i++) {
        ESP_LOGI(TAG, "  skill[%d]: name=%s, hot=%d, usage=%d",
                 i, s_skills[i].name, SKILL_IS_HOT(s_skills[i]), s_skills[i].usage_count);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *skills_arr = cJSON_CreateArray();

    for (int i = 0; i < s_skill_count; i++) {
        cJSON *skill = cJSON_CreateObject();
        cJSON_AddStringToObject(skill, "name", s_skills[i].name);
        cJSON_AddStringToObject(skill, "path", s_skills[i].path);
        cJSON_AddBoolToObject(skill, "is_auto", SKILL_IS_AUTO(s_skills[i]));
        cJSON_AddNumberToObject(skill, "usage_count", s_skills[i].usage_count);
        cJSON_AddNumberToObject(skill, "success_rate", s_skills[i].success_rate);
        cJSON_AddBoolToObject(skill, "is_hot", SKILL_IS_HOT(s_skills[i]));
        if (s_skills[i].last_used > 0) {
            cJSON_AddNumberToObject(skill, "last_used", (double)s_skills[i].last_used);
        }
        cJSON_AddItemToArray(skills_arr, skill);
    }

    cJSON_AddItemToObject(root, "skills", skills_arr);
    cJSON_AddNumberToObject(root, "hot_threshold", SKILL_META_HOT_THRESHOLD);

    char *json_str = cJSON_PrintUnformatted(root);
    size_t len = 0;
    if (json_str) {
        len = strlen(json_str);
        if (len >= size) {
            len = size - 1;
        }
        memcpy(buf, json_str, len);
        buf[len] = '\0';
        free(json_str);
    }

    cJSON_Delete(root);
    return len;
}

size_t skill_meta_get_all_auto_skills(char *buf, size_t size)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (!buf || size == 0) {
        return 0;
    }

    size_t off = 0;
    int count = s_skill_count < SKILL_META_MAX_SKILLS ? s_skill_count : SKILL_META_MAX_SKILLS;

    for (int i = 0; i < count && off < size - 1; i++) {
        /* Include all auto skills, not just hot ones */
        ESP_LOGI(TAG, "Checking skill[%d]: name=%s, path='%s', auto=%d",
                 i, s_skills[i].name, s_skills[i].path, SKILL_IS_AUTO(s_skills[i]));

        if (SKILL_IS_AUTO(s_skills[i]) && s_skills[i].path[0] != '\0') {
            /* Safety: verify file exists and check size before reading */
            struct stat st;
            if (stat(s_skills[i].path, &st) != 0) {
                ESP_LOGW(TAG, "skill file stat failed: %s", s_skills[i].path);
                continue;
            }
            if (st.st_size > SKILL_BUFFER_SIZE - 1) {
                ESP_LOGW(TAG, "skill file too large: %s (%ld bytes)",
                         s_skills[i].path, (long)st.st_size);
                continue;
            }

            /* Load full content of auto skill using static buffer */
            FILE *f = fopen(s_skills[i].path, "r");
            if (!f) {
                ESP_LOGW(TAG, "Failed to open skill file: %s", s_skills[i].path);
                continue;
            }

            size_t n = fread(s_skill_buffer, 1, SKILL_BUFFER_SIZE - 1, f);
            s_skill_buffer[n] = '\0';
            fclose(f);

            /* Skip YAML frontmatter */
            char *start = s_skill_buffer;
            if (strncmp(start, "---", 3) == 0) {
                char *end = strstr(start + 3, "---");
                if (end) {
                    start = end + 3;
                    while (*start == '\n' || *start == '\r') start++;
                }
            }

            if (off > 0 && off < size - 4) {
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

esp_err_t skill_meta_record_skill_usage(const char *tool_name,
                                        const char *tool_input,
                                        bool success)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (!tool_name) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Search all auto-skills (not just hot) for one whose tool sequence contains this tool */
    for (int i = 0; i < s_skill_count; i++) {
        if (!SKILL_IS_AUTO(s_skills[i])) {
            continue;
        }

        /* Load skill content to find Tool Sequence section */
        FILE *f = fopen(s_skills[i].path, "r");
        if (!f) continue;

        size_t n = fread(s_skill_buffer, 1, SKILL_BUFFER_SIZE - 1, f);
        s_skill_buffer[n] = '\0';
        fclose(f);

        /* Find "## Tool Sequence" or "## Tool Sequence\n" section */
        char *seq_start = strstr(s_skill_buffer, "## Tool Sequence");
        if (!seq_start) continue;

        /* Skip to after the header */
        seq_start += strlen("## Tool Sequence");
        while (*seq_start == '\n' || *seq_start == ' ') seq_start++;

        /* Find the next section header to limit search scope */
        char *seq_end = strstr(seq_start, "\n## ");
        char seq_saved = 0;
        if (seq_end) {
            seq_saved = *seq_end;
            *seq_end = '\0';  /* Temporarily limit search scope */
        }

        /* Check if tool_name appears in this skill's tool sequence */
        /* Tool sequence format is:
         *   First tool:  "1. tool_name" or "1. tool_name(input)"
         *   Other tools: "\nN. tool_name" or "\nN. tool_name(input)" */
        char tool_pattern[128];
        snprintf(tool_pattern, sizeof(tool_pattern), "%s(", tool_name);

        /* Check first tool at seq_start (no preceding \n) */
        char first_tool[64];
        snprintf(first_tool, sizeof(first_tool), "1. %s", tool_name);
        if (strncmp(seq_start, first_tool, strlen(first_tool)) == 0) {
            ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                     tool_name, s_skills[i].name);
            esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
            return err;
        }
        /* First tool with input: "1. tool_name(" */
        char first_tool_input[128];
        snprintf(first_tool_input, sizeof(first_tool_input), "1. %s(", tool_name);
        if (strncmp(seq_start, first_tool_input, strlen(first_tool_input)) == 0) {
            ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                     tool_name, s_skills[i].name);
            esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
            return err;
        }

        /* Check tools at positions 2-9 (preceded by \n) */
        char tool_check[64];
        for (int d = 2; d <= 9; d++) {
            snprintf(tool_check, sizeof(tool_check), "\n%d. %s", d, tool_name);
            if (strstr(seq_start, tool_check) != NULL) {
                ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                         tool_name, s_skills[i].name);
                esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
                return err;
            }
            /* Tool with input: \nN. tool_name( */
            snprintf(tool_check, sizeof(tool_check), "\n%d. %s(", d, tool_name);
            if (strstr(seq_start, tool_check) != NULL) {
                ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                         tool_name, s_skills[i].name);
                esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
                return err;
            }
        }

        /* Also check for tool_name( anywhere in sequence (for completeness) */
        if (strstr(seq_start, tool_pattern) != NULL) {
            if (seq_end) *seq_end = seq_saved;
            ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                     tool_name, s_skills[i].name);
            esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
            return err;
        }

        /* Restore buffer before next iteration */
        if (seq_end) *seq_end = seq_saved;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t skill_meta_save(void)
{
    return save_to_file();
}

/* ─── Jaccard similarity helpers ──────────────────────────────────────── */

#define JACCARD_MAX_TOKENS 64
#define JACCARD_THRESHOLD  0.3f

/* Tokenize a string into words. Chinese characters become individual tokens.
 * Returns number of tokens written to out[][]. */
static int tokenize(const char *str, char out[][32], int max)
{
    if (!str || !out || max <= 0) return 0;

    int count = 0;
    const char *p = str;

    while (*p && count < max) {
        unsigned char c = (unsigned char)*p;

        if (c < 0x80) {
            /* ASCII: skip whitespace/punctuation, collect word */
            if (isspace(c) || ispunct(c)) {
                p++;
                continue;
            }
            int len = 0;
            while (*p && !isspace((unsigned char)*p) && !ispunct((unsigned char)*p) && len < 31) {
                out[count][len++] = tolower((unsigned char)*p);
                p++;
            }
            out[count][len] = '\0';
            if (len > 0) count++;
        } else if ((c & 0xE0) == 0xC0) {
            /* 2-byte UTF-8 (rare CJK) */
            out[count][0] = p[0]; out[count][1] = p[1]; out[count][2] = '\0';
            p += 2; count++;
        } else if ((c & 0xF0) == 0xE0) {
            /* 3-byte UTF-8 (most CJK) */
            out[count][0] = p[0]; out[count][1] = p[1]; out[count][2] = p[2]; out[count][3] = '\0';
            p += 3; count++;
        } else if ((c & 0xF8) == 0xF0) {
            /* 4-byte UTF-8 */
            out[count][0] = p[0]; out[count][1] = p[1]; out[count][2] = p[2]; out[count][3] = p[3]; out[count][4] = '\0';
            p += 4; count++;
        } else {
            p++; /* skip continuation bytes */
        }
    }
    return count;
}

/* Check if token exists in set */
static bool token_in(const char *token, char set[][32], int set_size)
{
    for (int i = 0; i < set_size; i++) {
        if (strcmp(token, set[i]) == 0) return true;
    }
    return false;
}

/* Compute Jaccard similarity: |A ∩ B| / |A ∪ B|
 * Uses PSRAM for token buffers to avoid stack overflow on ESP32. */
static float jaccard(const char *a, const char *b)
{
    char (*tokens_a)[32] = heap_caps_malloc(JACCARD_MAX_TOKENS * 32, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char (*tokens_b)[32] = heap_caps_malloc(JACCARD_MAX_TOKENS * 32, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tokens_a || !tokens_b) {
        free(tokens_a);
        free(tokens_b);
        return 0.0f;
    }

    int na = tokenize(a, tokens_a, JACCARD_MAX_TOKENS);
    int nb = tokenize(b, tokens_b, JACCARD_MAX_TOKENS);

    if (na == 0 && nb == 0) { free(tokens_a); free(tokens_b); return 1.0f; }
    if (na == 0 || nb == 0) { free(tokens_a); free(tokens_b); return 0.0f; }

    /* Deduplicate tokens_a */
    int unique_a = 0;
    for (int i = 0; i < na; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(tokens_a[i], tokens_a[j]) == 0) { dup = true; break; }
        }
        if (!dup) {
            if (unique_a != i) strcpy(tokens_a[unique_a], tokens_a[i]);
            unique_a++;
        }
    }

    /* Deduplicate tokens_b */
    int unique_b = 0;
    for (int i = 0; i < nb; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(tokens_b[i], tokens_b[j]) == 0) { dup = true; break; }
        }
        if (!dup) {
            if (unique_b != i) strcpy(tokens_b[unique_b], tokens_b[i]);
            unique_b++;
        }
    }

    /* Count intersection */
    int intersection = 0;
    for (int i = 0; i < unique_a; i++) {
        if (token_in(tokens_a[i], tokens_b, unique_b)) intersection++;
    }

    int union_size = unique_a + unique_b - intersection;
    float result = union_size > 0 ? (float)intersection / union_size : 0.0f;

    free(tokens_a);
    free(tokens_b);
    return result;
}

/* Build a composite text from skill metadata for comparison */
static void skill_text(const skill_meta_t *s, char *buf, size_t size)
{
    int off = snprintf(buf, size, "%s", s->name);
    if (s->description[0] && off < size - 1)
        off += snprintf(buf + off, size - off, " %s", s->description);
    if (s->one_line_summary[0] && off < size - 1)
        off += snprintf(buf + off, size - off, " %s", s->one_line_summary);
    for (int t = 0; t < s->tag_count && off < size - 1; t++)
        off += snprintf(buf + off, size - off, " %s", s->tags[t]);
}

/* ─── Public: Jaccard-based similarity check ──────────────────────────── */

bool skill_meta_similar_exists_jaccard(const char *new_intent, char *similar_skill_name, size_t name_size)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (!new_intent || !similar_skill_name) {
        return false;
    }

    bool found = false;
    float best_score = 0.0f;
    char best_name[64] = {0};

    for (int i = 0; i < s_skill_count; i++) {
        if (!SKILL_IS_AUTO(s_skills[i])) continue;

        char s_text[640];
        skill_text(&s_skills[i], s_text, sizeof(s_text));

        float score = jaccard(new_intent, s_text);
        if (score > best_score) {
            best_score = score;
            strncpy(best_name, s_skills[i].name, sizeof(best_name) - 1);
        }
    }

    if (best_score >= JACCARD_THRESHOLD) {
        ESP_LOGI(TAG, "Jaccard similar skill found: %s (score=%.2f)", best_name, best_score);
        strncpy(similar_skill_name, best_name, name_size - 1);
        similar_skill_name[name_size - 1] = '\0';
        found = true;
    } else {
        ESP_LOGI(TAG, "Jaccard no similar skill (best=%.2f, threshold=%.2f)", best_score, JACCARD_THRESHOLD);
    }

    return found;
}

int skill_meta_get_quality_score(const skill_meta_t *meta)
{
    if (!meta) {
        return 0;
    }
    return (int)SKILL_QUALITY_SCORE(*meta);
}