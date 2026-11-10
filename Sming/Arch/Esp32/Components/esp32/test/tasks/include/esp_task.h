#pragma once
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#define IRAM_ATTR
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define CONFIG_LWIP_TCPIP_TASK_STACK_SIZE 4096
#define CONFIG_LWIP_TCPIP_TASK_PRIO 18
#define ESP_OK 0
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
inline std::atomic<unsigned> yields;
#define portYIELD_FROM_ISR_ARG(value) (yields += unsigned(bool(value)))
using BaseType_t = int;
using portMUX_TYPE = std::mutex;
inline std::function<void()> afterUnlock;
#define portMUX_INITIALIZER_UNLOCKED {}
inline void enterCritical(portMUX_TYPE* mux) { mux->lock(); }
inline void exitCritical(portMUX_TYPE* mux)
{
	mux->unlock();
	if(afterUnlock) {
		auto callback = std::move(afterUnlock);
		afterUnlock = {};
		callback();
	}
}
#define portENTER_CRITICAL_SAFE(mux) enterCritical(mux)
#define portEXIT_CRITICAL_SAFE(mux) exitCritical(mux)
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, unsigned, void*, unsigned) {}
struct Queue {
	unsigned capacity;
	std::deque<os_event_t> events;
	std::mutex mutex;
};
using QueueHandle_t = Queue*;
inline QueueHandle_t xQueueCreate(unsigned count, unsigned) { return new Queue{count, {}, {}}; }
inline int xQueueSendToBackFromISR(QueueHandle_t q, const os_event_t* event, BaseType_t* woken)
{
	*woken = pdFALSE;
	std::lock_guard<std::mutex> guard(q->mutex);
	if(q->events.size() == q->capacity) return pdFALSE;
	q->events.push_back(*event);
	return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, os_event_t* event, unsigned)
{
	std::lock_guard<std::mutex> guard(q->mutex);
	if(q->events.empty()) return pdFALSE;
	*event = q->events.front();
	q->events.pop_front();
	return pdTRUE;
}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q)
{
	std::lock_guard<std::mutex> guard(q->mutex);
	return q->events.size();
}
inline int xQueueIsQueueEmptyFromISR(QueueHandle_t q)
{
	return uxQueueMessagesWaiting(q) == 0 ? pdTRUE : pdFALSE;
}
