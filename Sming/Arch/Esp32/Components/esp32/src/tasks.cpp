#include "include/esp_tasks.h"
#include <esp_task.h>
#include <debug_progmem.h>
#include <esp_timer.h>

#ifndef DISABLE_NETWORK
#include <lwip/tcpip.h>
#endif

namespace
{
os_task_t taskCallback;
QueueHandle_t eventQueue;

#ifndef SMING_RELEASE
DRAM_ATTR portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR sming_dispatch_stats_t dispatchStats{};
#endif

#ifdef DISABLE_NETWORK

void sming_task_loop(void*)
{
	os_event_t evt;
	while(xQueueReceive(eventQueue, &evt, portMAX_DELAY) == pdTRUE) {
		taskCallback(&evt);
	}
}

#else

tcpip_callback_msg* callbackMessage;
volatile bool eventQueueFlag;

#ifndef SMING_RELEASE
DRAM_ATTR sming_dispatch_trace_entry_t traceEntries[SMING_DISPATCH_TRACE_LENGTH]{};
DRAM_ATTR sming_dispatch_trace_entry_t traceCurrent{};
DRAM_ATTR sming_dispatch_trace_entry_t traceFailedCurrent{};
DRAM_ATTR uint64_t traceFailedUs;
DRAM_ATTR uint32_t traceNext;
DRAM_ATTR uint32_t traceCount;
DRAM_ATTR uint32_t traceOverwritten;
DRAM_ATTR uint32_t traceBatch;
DRAM_ATTR uint32_t traceFailures;
DRAM_ATTR uint32_t traceQueueDepth;
DRAM_ATTR bool traceActive;
DRAM_ATTR bool traceFailedActive;
#endif

/**
 * @brief Process all messages in the Sming event queue
 * Called in the context of the tcpip message thread.
 */
void tcpip_message_handler(void*)
{
#ifndef SMING_RELEASE
	const auto startedUs = esp_timer_get_time();
#endif
	eventQueueFlag = false;
	// Process only waiting messages (not newly posted ones) so tcpip doesn't starve
	auto messageCount = uxQueueMessagesWaiting(eventQueue);
#ifndef SMING_RELEASE
	const auto queued = messageCount;
	uint32_t processed = 0;
	portENTER_CRITICAL(&statsMux);
	const auto batch = ++traceBatch;
	portEXIT_CRITICAL(&statsMux);
#endif
	os_event_t evt;
	while(messageCount-- && xQueueReceive(eventQueue, &evt, 0) == pdTRUE) {
#ifndef SMING_RELEASE
		portENTER_CRITICAL(&statsMux);
		traceCurrent = {uint64_t(esp_timer_get_time()), 0, batch, evt.sig, evt.par};
		traceActive = true;
		portEXIT_CRITICAL(&statsMux);
#endif
		taskCallback(&evt);
#ifndef SMING_RELEASE
		portENTER_CRITICAL(&statsMux);
		if(traceFailures == 0) {
			traceCurrent.elapsed_us = uint32_t(uint64_t(esp_timer_get_time()) - traceCurrent.started_us);
			traceEntries[traceNext] = traceCurrent;
			traceNext = (traceNext + 1) % SMING_DISPATCH_TRACE_LENGTH;
			if(traceCount < SMING_DISPATCH_TRACE_LENGTH) {
				++traceCount;
			} else {
				++traceOverwritten;
			}
		}
		traceActive = false;
		portEXIT_CRITICAL(&statsMux);
		++processed;
#endif
	}
#ifndef SMING_RELEASE
	const auto elapsedUs = uint32_t(esp_timer_get_time() - startedUs);
	portENTER_CRITICAL(&statsMux);
	++dispatchStats.batches;
	dispatchStats.events += processed;
	dispatchStats.batch_total_us += elapsedUs;
	if(elapsedUs > dispatchStats.batch_max_us) {
		dispatchStats.batch_max_us = elapsedUs;
	}
	if(processed > dispatchStats.batch_max_events) {
		dispatchStats.batch_max_events = processed;
	}
	if(queued > dispatchStats.sampled_queue_peak) {
		dispatchStats.sampled_queue_peak = queued;
	}
	portEXIT_CRITICAL(&statsMux);
#endif
}

/**
 * @brief Kick the tcpip message handling thread so we get a callback
 * @retval bool Woken flag, true if a message was queued, false for no action
 * @note Called from system_os_post(), which must be ISR-safe even while the flash cache
 * is disabled (e.g. IRAM interrupt handlers such as the LEDC fade ISR), so must be in IRAM.
 */
bool IRAM_ATTR tcpip_kick_thread()
{
	// If queue isn't empty and we haven't already asked for a tcpip callback, do that now
	if(xQueueIsQueueEmptyFromISR(eventQueue) == pdFALSE && !eventQueueFlag) {
		eventQueueFlag = true;
		auto err = tcpip_callbackmsg_trycallback_fromisr(callbackMessage);
#ifndef SMING_RELEASE
		portENTER_CRITICAL_ISR(&statsMux);
		++dispatchStats.mailbox_post_attempts;
		if(err == ERR_MEM) {
			++dispatchStats.mailbox_post_failures;
			if(traceFailures == 0) {
				traceFailedUs = uint64_t(esp_timer_get_time());
				traceQueueDepth = uxQueueMessagesWaitingFromISR(eventQueue);
				traceFailedActive = traceActive;
				if(traceActive) {
					traceFailedCurrent = traceCurrent;
					traceFailedCurrent.elapsed_us = uint32_t(traceFailedUs - traceCurrent.started_us);
				}
			}
			++traceFailures;
		}
		portEXIT_CRITICAL_ISR(&statsMux);
#endif
		return (err == ERR_NEED_SCHED);
	}

	return false;
}

#endif

} // namespace

bool sming_dispatch_trace_take(sming_dispatch_trace_t* trace)
{
#if !defined(SMING_RELEASE) && !defined(DISABLE_NETWORK)
	if(trace == nullptr) {
		debug_e("TQ: Null dispatcher trace output");
		return false;
	}
	portENTER_CRITICAL(&statsMux);
	if(traceFailures == 0) {
		portEXIT_CRITICAL(&statsMux);
		return false;
	}
	trace->failed_us = traceFailedUs;
	trace->queue_depth = traceQueueDepth;
	trace->failures = traceFailures;
	trace->count = traceCount;
	trace->overwritten = traceOverwritten;
	trace->active = traceFailedActive;
	trace->current = traceFailedActive ? traceFailedCurrent : sming_dispatch_trace_entry_t{};
	const auto first = (traceNext + SMING_DISPATCH_TRACE_LENGTH - traceCount) % SMING_DISPATCH_TRACE_LENGTH;
	for(uint32_t i = 0; i < traceCount; ++i) {
		trace->entries[i] = traceEntries[(first + i) % SMING_DISPATCH_TRACE_LENGTH];
	}
	traceFailures = 0;
	traceCount = 0;
	traceNext = 0;
	traceOverwritten = 0;
	portEXIT_CRITICAL(&statsMux);
	return true;
#else
	(void)trace;
	return false;
#endif
}

bool sming_dispatch_stats_take(sming_dispatch_stats_t* stats)
{
#if !defined(SMING_RELEASE) && !defined(DISABLE_NETWORK)
	if(stats == nullptr) {
		debug_e("TQ: Null dispatcher statistics output");
		return false;
	}
	portENTER_CRITICAL(&statsMux);
	*stats = dispatchStats;
	dispatchStats = {};
	portEXIT_CRITICAL(&statsMux);
	return true;
#else
	(void)stats;
	return false;
#endif
}

bool system_os_task(os_task_t callback, uint8_t prio, os_event_t* queue, uint8_t qlen)
{
	if(callback == nullptr || queue == nullptr || qlen == 0) {
		debug_e("TQ: Bad parameters");
		return false;
	}

	if(prio != USER_TASK_PRIO_1) {
		debug_e("TQ: Invalid priority %u", prio);
		return false;
	}

	if(taskCallback != nullptr) {
		debug_w("TQ: Queue %u already initialised", prio);
		return false;
	}

	eventQueue = xQueueCreate(qlen, sizeof(os_event_t));
	if(eventQueue == nullptr) {
		return false;
	}

	taskCallback = callback;

	return true;
}

void start_sming_task_loop()
{
#ifdef DISABLE_NETWORK

#if defined(SOC_ESP32) && !CONFIG_FREERTOS_UNICORE
	constexpr unsigned core_id{1};
#else
	constexpr unsigned core_id{0};
#endif
	xTaskCreatePinnedToCore(sming_task_loop, "Sming", CONFIG_LWIP_TCPIP_TASK_STACK_SIZE, nullptr,
							CONFIG_LWIP_TCPIP_TASK_PRIO, nullptr, core_id);

#else

	callbackMessage = tcpip_callbackmsg_new(tcpip_callback_fn(tcpip_message_handler), nullptr);

	// There may be messages already queued
	tcpip_kick_thread();

#endif
}

bool IRAM_ATTR system_os_post(uint8_t prio, os_signal_t sig, os_param_t par)
{
	if(prio != USER_TASK_PRIO_1) {
		return false;
	}

	os_event_t ev{sig, par};
	BaseType_t woken;
	auto res = xQueueSendToBackFromISR(eventQueue, &ev, &woken);
	if(res != pdTRUE) {
#ifndef SMING_RELEASE
		portENTER_CRITICAL_ISR(&statsMux);
		++dispatchStats.event_post_failures;
		portEXIT_CRITICAL_ISR(&statsMux);
#endif
		return false;
	}

#ifndef DISABLE_NETWORK
	if(!callbackMessage) {
		// Message loop not yet active
		return true;
	}
	woken = tcpip_kick_thread();
#endif

	portYIELD_FROM_ISR_ARG(woken);

	return true;
}
