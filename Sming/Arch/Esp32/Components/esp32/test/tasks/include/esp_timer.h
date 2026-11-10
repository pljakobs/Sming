#pragma once
enum { ESP_TIMER_TASK };
struct esp_timer_create_args_t {
	void (*callback)(void*);
	void* arg{};
	int dispatch_method;
	const char* name;
};
struct FakeTimer {
	esp_timer_create_args_t args;
	std::atomic<bool> armed{};
};
using esp_timer_handle_t = FakeTimer*;
inline int64_t fakeTime;
inline bool failTimerAllocation;
inline bool failTimerArm;
inline int64_t esp_timer_get_time() { return fakeTime; }
inline std::vector<FakeTimer*> timers;
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* result)
{
	if(failTimerAllocation) return ESP_ERR_NO_MEM;
	*result = new FakeTimer{*args};
	timers.push_back(*result);
	return ESP_OK;
}
inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t)
{
	if(failTimerArm) return ESP_ERR_NO_MEM;
	if(!timer || timer->armed.exchange(true)) return ESP_ERR_INVALID_STATE;
	return ESP_OK;
}
inline int esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t interval)
{
	return esp_timer_start_once(timer, interval);
}
