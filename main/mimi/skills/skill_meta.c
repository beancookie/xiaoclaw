#include "skills/skill_meta.h"
#include "mimi_config.h"
#include "util/fatfs_util.h"
#include "llm/llm_proxy.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "skill_meta";

/* Static buffer for parsing - avoids stack overflow */
static char s_skill_buffer[8192];

/* In-memory cache of skill metadata */
static skill_meta_t s_skills[SKILL_META_MAX_SKILLS];
static int s_skill_count = 0;
static bool s_initialized = false;
static time_t s_last_save = 0;

/* ─── Helper: compute simple hash for skill name generation ───────────── */

static void simple_hash(const char *str, char *out, size_t out_size)
{
    unsigned int hash = 5381;
    for (const char *p = str; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    snprintf(out, out_size, "%04x", hash & 0xffff);
}

/* ─── Helper: save metadata to FATFS ─────────────────────────────────── */

static esp_err_t save_to_file(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *skills_arr = cJSON_CreateArray();

    for (int i = 0; i < s_skill_count; i++) {
        cJSON *skill = cJSON_CreateObject();
        cJSON_AddStringToObject(skill, "name", s_skills[i].name);
        cJSON_AddStringToObject(skill, "path", s_skills[i].path);
        cJSON_AddBoolToObject(skill, "is_auto", s_skills[i].is_auto);
        cJSON_AddNumberToObject(skill, "usage_count", s_skills[i].usage_count);
        cJSON_AddNumberToObject(skill, "success_count", s_skills[i].success_count);
        cJSON_AddNumberToObject(skill, "success_rate", s_skills[i].success_rate);
        cJSON_AddNumberToObject(skill, "last_used", (double)s_skills[i].last_used);
        cJSON_AddBoolToObject(skill, "is_hot", s_skills[i].is_hot);

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

    /* Read file content */
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
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

        cJSON *is_auto = cJSON_GetObjectItem(skill, "is_auto");
        if (is_auto) m->is_auto = cJSON_IsTrue(is_auto);

        cJSON *usage = cJSON_GetObjectItem(skill, "usage_count");
        if (usage) m->usage_count = (int)usage->valueint;

        cJSON *success = cJSON_GetObjectItem(skill, "success_count");
        if (success) m->success_count = (int)success->valueint;

        cJSON *rate = cJSON_GetObjectItem(skill, "success_rate");
        if (rate) m->success_rate = (float)rate->valuedouble;

        cJSON *last_used = cJSON_GetObjectItem(skill, "last_used");
        if (last_used) m->last_used = (time_t)last_used->valueint;

        cJSON *is_hot = cJSON_GetObjectItem(skill, "is_hot");
        if (is_hot) m->is_hot = cJSON_IsTrue(is_hot);

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

/* ─── Helper: update hot flag ─────────────────────────────────────────── */

static void update_hot_flag(skill_meta_t *m)
{
    m->is_hot = (m->usage_count >= SKILL_META_HOT_THRESHOLD);
}

/* ─── Public API ──────────────────────────────────────────────────────── */

esp_err_t skill_meta_init(void)
{
    if (s_initialized) {
        return ESP_OK;
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
                m->is_auto = true;
                m->usage_count = 0;
                m->success_rate = 0.0;
                m->last_used = 0;
                m->is_hot = false;
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
    update_hot_flag(m);

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
    m->is_auto = meta->is_auto;
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
    update_hot_flag(m);
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
        ESP_LOGI(TAG, "  skill[%d]: name=%s, is_auto=%d, usage=%d",
                 i, s_skills[i].name, s_skills[i].is_auto, s_skills[i].usage_count);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *skills_arr = cJSON_CreateArray();

    for (int i = 0; i < s_skill_count; i++) {
        cJSON *skill = cJSON_CreateObject();
        cJSON_AddStringToObject(skill, "name", s_skills[i].name);
        cJSON_AddStringToObject(skill, "path", s_skills[i].path);
        cJSON_AddBoolToObject(skill, "is_auto", s_skills[i].is_auto);
        cJSON_AddNumberToObject(skill, "usage_count", s_skills[i].usage_count);
        cJSON_AddNumberToObject(skill, "success_rate", s_skills[i].success_rate);
        cJSON_AddBoolToObject(skill, "is_hot", s_skills[i].is_hot);
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

size_t skill_meta_get_hot_skills(char *buf, size_t size)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    size_t off = 0;
    for (int i = 0; i < s_skill_count && off < size - 1; i++) {
        if (s_skills[i].is_hot && s_skills[i].is_auto) {
            /* Safety: verify file exists and check size before reading */
            struct stat st;
            if (stat(s_skills[i].path, &st) != 0) continue;
            if (st.st_size > sizeof(s_skill_buffer) - 1) continue;

            /* Load full content of hot auto skill using static buffer */
            FILE *f = fopen(s_skills[i].path, "r");
            if (!f) continue;

            size_t n = fread(s_skill_buffer, 1, sizeof(s_skill_buffer) - 1, f);
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
        ESP_LOGI(TAG, "Checking skill[%d]: name=%s, path='%s', is_auto=%d",
                 i, s_skills[i].name, s_skills[i].path, s_skills[i].is_auto);

        if (s_skills[i].is_auto && s_skills[i].path[0] != '\0') {
            /* Safety: verify file exists and check size before reading */
            struct stat st;
            if (stat(s_skills[i].path, &st) != 0) {
                ESP_LOGW(TAG, "skill file stat failed: %s", s_skills[i].path);
                continue;
            }
            if (st.st_size > sizeof(s_skill_buffer) - 1) {
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

            size_t n = fread(s_skill_buffer, 1, sizeof(s_skill_buffer) - 1, f);
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
        if (!s_skills[i].is_auto) {
            continue;
        }

        /* Load skill content to find Tool Sequence section */
        FILE *f = fopen(s_skills[i].path, "r");
        if (!f) continue;

        size_t n = fread(s_skill_buffer, 1, sizeof(s_skill_buffer) - 1, f);
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
        if (seq_end) {
            *seq_end = '\0';  /* NUL terminate to limit search */
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
            ESP_LOGI(TAG, "Tool %s matched skill %s, recording usage",
                     tool_name, s_skills[i].name);
            esp_err_t err = skill_meta_record_usage(s_skills[i].name, success);
            return err;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t skill_meta_save(void)
{
    return save_to_file();
}

bool skill_meta_similar_exists(const char *intent)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (!intent) {
        return false;
    }

    /* Compute hash of the new intent */
    char intent_hash[5];
    simple_hash(intent, intent_hash, sizeof(intent_hash));

    for (int i = 0; i < s_skill_count; i++) {
        if (s_skills[i].is_auto) {
            const char *name = s_skills[i].name;

            /* Skill name format: auto_<prefix>_<4_char_hash><4_char_timestamp> */
            /* Find the last underscore to locate the hash portion */
            const char *last_underscore = strrchr(name, '_');
            if (!last_underscore) continue;

            /* Hash is after the last underscore, before the 4-digit timestamp */
            if (strlen(last_underscore + 1) < 8) continue;

            /* Compare first 4 chars (the intent hash) */
            if (strncmp(intent_hash, last_underscore + 1, 4) == 0) {
                ESP_LOGI(TAG, "Similar skill found: %s", name);
                return true;
            }
        }
    }
    return false;
}

bool skill_meta_similar_exists_llm(const char *new_intent, char *similar_skill_name, size_t name_size)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    if (!new_intent || !similar_skill_name) {
        return false;
    }

    /* Build prompt with existing skills */
    char skills_context[8192];
    size_t ctx_len = skill_meta_get_all_auto_skills(skills_context, sizeof(skills_context));
    if (ctx_len == 0) {
        ESP_LOGI(TAG, "No existing auto-skills to compare against");
        return false;
    }

    char prompt[16384];
    int len = snprintf(prompt, sizeof(prompt),
        "判断以下新任务是否与已有技能相似。\n\n"
        "新任务: %s\n\n"
        "已有技能:\n%s\n\n"
        "如果新任务与某个已有技能目标相同或工具序列相似，返回该技能名称（格式：SIMILAR: skill_name）。"
        "如果不相似，返回：NO_MATCH\n",
        new_intent, skills_context);
    if (len >= sizeof(prompt)) {
        ESP_LOGW(TAG, "Prompt too long, truncated");
    }

    /* Build messages array for LLM - reference runner.c pattern */
    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        return false;
    }

    cJSON *msg = cJSON_CreateObject();
    if (!msg) {
        cJSON_Delete(messages);
        return false;
    }
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt);
    cJSON_AddItemToArray(messages, msg);

    /* Call LLM without tools (text-only response) - reference runner.c:243 */
    llm_response_t resp;
    esp_err_t err = llm_chat_tools(NULL, messages, NULL, &resp);

    cJSON_Delete(messages);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LLM similar skill check failed: %s, allowing crystallization", esp_err_to_name(err));
        return false;  /* Fail open - allow crystallization */
    }

    if (!resp.text || resp.text_len == 0) {
        ESP_LOGW(TAG, "LLM returned empty response, allowing crystallization");
        llm_response_free(&resp);
        return false;
    }

    ESP_LOGI(TAG, "LLM similarity check response: %.*s", resp.text_len > 100 ? 100 : (int)resp.text_len, resp.text);

    /* Parse response */
    bool found = false;
    if (strncmp(resp.text, "SIMILAR:", 8) == 0) {
        const char *name = resp.text + 8;
        while (*name == ' ') name++;  /* Skip spaces */
        strncpy(similar_skill_name, name, name_size - 1);
        similar_skill_name[name_size - 1] = '\0';
        /* Remove trailing newline or whitespace */
        for (int i = strlen(similar_skill_name) - 1; i >= 0; i--) {
            if (similar_skill_name[i] == '\n' || similar_skill_name[i] == '\r' || similar_skill_name[i] == ' ') {
                similar_skill_name[i] = '\0';
            } else {
                break;
            }
        }
        ESP_LOGI(TAG, "LLM found similar skill: %s", similar_skill_name);
        found = true;
    } else {
        ESP_LOGI(TAG, "LLM found no similar skill");
    }

    llm_response_free(&resp);
    return found;
}

int skill_meta_get_quality_score(const skill_meta_t *meta)
{
    if (!meta) {
        return 0;
    }
    return (int)SKILL_QUALITY_SCORE(*meta);
}

int skill_meta_get_hot_names(char names[][64], int max)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    int count = 0;
    for (int i = 0; i < s_skill_count && count < max; i++) {
        if (s_skills[i].is_hot) {
            strncpy(names[count], s_skills[i].name, 63);
            names[count][63] = '\0';
            count++;
        }
    }
    return count;
}