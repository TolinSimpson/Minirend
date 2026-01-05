#ifndef SOKOL_LOG_H
#define SOKOL_LOG_H

/* Minimal sokol_log.h shim.
 *
 * Newer/older sokol releases sometimes ship `sokol_log.h` as an optional header.
 * The cosmo-sokol build includes it from `src/platform/sokol_shared.c`.
 *
 * For minirend we don't rely on it; the runtime uses its own logging.
 * This stub exists to keep builds working across sokol header versions.
 */

#include <stdint.h>

typedef void (*slog_func_t)(
    const char* tag,
    uint32_t log_level,
    uint32_t log_item_id,
    const char* message_or_null,
    uint32_t line_nr,
    const char* filename_or_null,
    void* user_data);

typedef struct slog_desc {
    slog_func_t func;
    void* user_data;
} slog_desc;

static inline void slog_setup(const slog_desc* desc) { (void)desc; }

#endif /* SOKOL_LOG_H */


