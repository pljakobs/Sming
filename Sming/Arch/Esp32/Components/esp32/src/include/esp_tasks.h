#pragma once
#include <c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t os_signal_t;
typedef uint32_t os_param_t;

typedef struct {
	os_signal_t sig;
	os_param_t par;
} os_event_t;

enum {
	USER_TASK_PRIO_1,
};

typedef void (*os_task_t)(os_event_t* e);

bool system_os_task(os_task_t task, uint8_t prio, os_event_t* queue, uint8_t qlen);
bool system_os_post(uint8_t prio, os_signal_t sig, os_param_t par);

typedef struct {
	uint64_t batch_total_us;
	uint32_t batches;
	uint32_t events;
	uint32_t batch_max_us;
	uint32_t batch_max_events;
	uint32_t sampled_queue_peak;
	uint32_t event_post_failures;
	uint32_t mailbox_post_attempts;
	uint32_t mailbox_post_failures;
} sming_dispatch_stats_t;

/** Snapshot and reset debug counters; false when unavailable. Not ISR-safe. */
bool sming_dispatch_stats_take(sming_dispatch_stats_t* stats);

enum { SMING_DISPATCH_TRACE_LENGTH = 32 };

typedef struct {
	uint64_t started_us;
	uint32_t elapsed_us;
	uint32_t batch;
	os_signal_t sig;
	os_param_t par;
} sming_dispatch_trace_entry_t;

typedef struct {
	uint64_t failed_us;
	uint32_t queue_depth;
	uint32_t failures;
	uint32_t count;
	uint32_t overwritten;
	bool active;
	sming_dispatch_trace_entry_t current;
	sming_dispatch_trace_entry_t entries[SMING_DISPATCH_TRACE_LENGTH];
} sming_dispatch_trace_t;

/** Take the first frozen mailbox-failure trace, oldest entry first, and rearm.
 * Returns false if none is pending or tracing is unavailable. Not ISR-safe.
 * current.elapsed_us is elapsed time at failure, not a completed duration.
 */
bool sming_dispatch_trace_take(sming_dispatch_trace_t* trace);

#ifdef __cplusplus
}
#endif
