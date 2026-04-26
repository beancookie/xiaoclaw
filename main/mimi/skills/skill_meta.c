#include "skills/skill_meta.h"
#include "mimi_config.h"
#include "util/fatfs_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "skill_meta";

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
        }

        cJSON *path = cJSON_GetObjectItem(skill, "path");
        if (path && path->valuestring) {
            strncpy(m->path, path->valuestring, sizeof(m->path) - 1);
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
                strncpy(m->path, skill_path, sizeof(m->path) - 1);
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
            /* Load full content of hot auto skill */
            char content[4096];
            FILE *f = fopen(s_skills[i].path, "r");
            if (!f) continue;

            size_t n = fread(content, 1, sizeof(content) - 1, f);
            content[n] = '\0';
            fclose(f);

            /* Skip YAML frontmatter */
            char *start = content;
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

esp_err_t skill_meta_save(void)
{
    return save_to_file();
}

bool skill_meta_similar_exists(const char *intent)
{
    if (!s_initialized) {
        skill_meta_init();
    }

    /* Extract first 3 significant words and check if any auto skill name contains similar */
    char intent_hash[8];
    simple_hash(intent, intent_hash, sizeof(intent_hash));

    for (int i = 0; i < s_skill_count; i++) {
        if (s_skills[i].is_auto) {
            /* Check if hash matches or if intent words appear in skill name */
            char name_hash[8];
            simple_hash(s_skills[i].name, name_hash, sizeof(name_hash));
            if (strncmp(intent_hash, name_hash, 4) == 0) {
                return true;
            }
        }
    }
    return false;
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