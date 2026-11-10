#ifdef TEST_RELEASE
#define NDEBUG
#endif
#include TASKS_SOURCE
#include <esp_timer.h>
#include <esp_system.h>
#ifdef TEST_RELEASE
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <random>
#include <thread>
#include <array>

unsigned handled;
bool repost;
std::vector<uint32_t> received;

void handle(os_event_t* event)
{
	++handled;
	received.push_back(event->par);
	if(repost) {
		repost = false;
		assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	}
}

void reset()
{
	delete eventQueue;
	eventQueue = nullptr;
	delete callbackMessage;
	callbackMessage = nullptr;
	for(auto* timer : timers) delete timer;
	timers.clear();
	mailbox.clear();
	taskCallback = nullptr;
	eventQueueFlag = false;
	rejectCount = 0;
	needsScheduling = false;
	handled = 0;
	repost = false;
	received.clear();
	yields = 0;
	afterUnlock = {};
	beforeMailboxPost = {};
	failTimerAllocation = false;
	failTimerArm = false;
	failCallbackAllocation = false;
	os_event_t queue[4]{};
	assert(system_os_task(handle, USER_TASK_PRIO_1, queue, 4));
}

void retry()
{
	assert(timers.size() == 1 && timers[0]->armed);
	auto* timer = timers[0];
	timer->armed = false;
	fakeTime += 1000;
	timer->args.callback(timer->args.arg);
}

void dispatch()
{
	assert(!mailbox.empty());
	auto* message = mailbox.front();
	mailbox.pop_front();
	message->callback(message->arg);
}

int main()
{
	reset();
	start_sming_task_loop();
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	dispatch();
	assert(handled == 1 && !eventQueueFlag);

	reset();
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	rejectCount = 1;
	start_sming_task_loop();
	assert(eventQueueFlag && mailbox.empty());
	if(!timers.empty() && timers[0]->armed) retry();
	if(!mailbox.empty()) dispatch();
	assert(handled == 1 && !eventQueueFlag);

	reset();
	start_sming_task_loop();
	rejectCount = 3;
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	retry();
	retry();
	assert(mailbox.empty() && handled == 0);
	retry();
	dispatch();
	assert(handled == 1);

	reset();
	start_sming_task_loop();
	rejectCount = 1;
	for(unsigned i = 0; i < 4; ++i) assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	assert(!system_os_post(USER_TASK_PRIO_1, 0, 0));
	retry();
	dispatch();
	assert(handled == 4 && eventQueue->events.empty());
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	dispatch();
	assert(handled == 5);

	reset();
	start_sming_task_loop();
	needsScheduling = true;
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	assert(!timers[0]->armed && yields == 1);
	dispatch();
	assert(handled == 1);

	reset();
	start_sming_task_loop();
	repost = true;
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	dispatch();
	assert(handled == 1 && mailbox.size() == 1);
	dispatch();
	assert(handled == 2 && mailbox.empty());

	// Another producer runs after the first has reserved the wakeup but before posting it.
	reset();
	start_sming_task_loop();
	afterUnlock = []() { assert(system_os_post(USER_TASK_PRIO_1, 0, 1)); };
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	assert(mailbox.size() == 1);
	dispatch();
	assert((received == std::vector<uint32_t>{0, 1}));

	// The same interleaving must not lose recovery when the reserved wakeup fails.
	reset();
	start_sming_task_loop();
	rejectCount = 1;
	beforeMailboxPost = []() { assert(system_os_post(USER_TASK_PRIO_1, 0, 1)); };
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	retry();
	dispatch();
	assert((received == std::vector<uint32_t>{0, 1}));

	// A producer takes over the wakeup after the retry clears its reservation.
	reset();
	start_sming_task_loop();
	rejectCount = 1;
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	afterUnlock = []() { assert(system_os_post(USER_TASK_PRIO_1, 0, 1)); };
	retry();
	assert(mailbox.size() == 1);
	dispatch();
	assert((received == std::vector<uint32_t>{0, 1}));

	// The dispatcher can already be running when scheduling its successor fails.
	reset();
	start_sming_task_loop();
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	rejectCount = 1;
	afterUnlock = []() { assert(system_os_post(USER_TASK_PRIO_1, 0, 1)); };
	dispatch();
	assert(handled == 2 && timers[0]->armed && mailbox.empty());
	retry();
	assert(mailbox.empty() && !eventQueueFlag);

	// Warning suppression must never suppress retries.
	reset();
	start_sming_task_loop();
	rejectCount = 1500;
	const auto previousWarnings = warnings;
	assert(system_os_post(USER_TASK_PRIO_1, 0, 0));
	for(unsigned i = 0; i < 1500; ++i) retry();
	assert(warnings - previousWarnings <= 2);
	dispatch();
	assert(handled == 1);

	// Seeded scheduling/failure stress: every accepted event must arrive exactly once, in order.
	std::mt19937 random(0x5a17);
	uint64_t totalAccepted = 0;
	for(unsigned run = 0; run < 1000; ++run) {
		reset();
		start_sming_task_loop();
		std::vector<uint32_t> accepted;
		for(unsigned step = 0; step < 1000; ++step) {
			switch(random() % 3) {
			case 0:
				if(system_os_post(USER_TASK_PRIO_1, 0, step)) accepted.push_back(step);
				break;
			case 1:
				if(!mailbox.empty()) dispatch();
				break;
			case 2:
				if(timers[0]->armed) retry();
				break;
			}
			if(random() % 5 == 0) rejectCount = random() % 5;
			assert(mailbox.size() <= 1);
		}
		rejectCount = 0;
		for(unsigned i = 0; i < 10 && (!mailbox.empty() || timers[0]->armed); ++i) {
			if(!mailbox.empty()) dispatch();
			if(timers[0]->armed) retry();
		}
		assert(eventQueue->events.empty());
		assert(!eventQueueFlag && mailbox.empty() && !timers[0]->armed);
		assert(received == accepted);
		totalAccepted += accepted.size();
	}

		// Startup must explicitly fail, not continue with a missing recovery timer or message.
		for(unsigned failure = 0; failure < 2; ++failure) {
			reset();
			failTimerAllocation = failure == 0;
			failCallbackAllocation = failure == 1;
			bool failed = false;
			try {
				start_sming_task_loop();
			} catch(const SystemAbort& error) {
				failed = error.details.find(failure == 0 ? "timer" : "message") != std::string::npos;
			}
			assert(failed);
		}

		reset();
		start_sming_task_loop();
		rejectCount = 1;
		failTimerArm = true;
		bool failed = false;
		try {
			system_os_post(USER_TASK_PRIO_1, 0, 0);
		} catch(const SystemAbort& error) {
			failed = error.details.find("Cannot arm") != std::string::npos;
		}
		assert(failed);

	// Four producers and a dispatcher/retry worker run on actual native threads.
	reset();
	start_sming_task_loop();
	constexpr unsigned producers = 4;
	constexpr unsigned postsPerProducer = 25000;
	std::vector<uint8_t> accepted(producers * postsPerProducer);
	std::atomic<bool> producersDone{};
	std::thread worker([&]() {
		std::mt19937 scheduling(0x8123);
		unsigned drainSteps = 0;
		do {
			tcpip_callback_msg* message{};
			{
				std::lock_guard<std::mutex> guard(mailboxMutex);
				if(!mailbox.empty()) {
					message = mailbox.front();
					mailbox.pop_front();
				}
				if(scheduling() % 3 == 0) rejectCount = scheduling() % 5;
			}
			if(message) message->callback(message->arg);
			if(timers[0]->armed.exchange(false)) {
				fakeTime += 1000;
				timers[0]->args.callback(timers[0]->args.arg);
			}
			std::this_thread::yield();
			if(producersDone) assert(++drainSteps < 1000000);
		} while(!producersDone || uxQueueMessagesWaiting(eventQueue) != 0 || timers[0]->armed);
	});
	std::vector<std::thread> threads;
	for(unsigned producer = 0; producer < producers; ++producer) {
		threads.emplace_back([&, producer]() {
			for(unsigned i = 0; i < postsPerProducer; ++i) {
				const auto id = producer * postsPerProducer + i;
				accepted[id] = system_os_post(USER_TASK_PRIO_1, 0, id);
				if(i % 32 == 0) std::this_thread::yield();
			}
		});
	}
	for(auto& thread : threads) thread.join();
	producersDone = true;
	worker.join();
	std::vector<uint8_t> delivered(accepted.size());
	std::array<int, producers> previous{};
	previous.fill(-1);
	for(const auto id : received) {
		assert(id < delivered.size() && ++delivered[id] == 1);
		const auto producer = id / postsPerProducer;
		assert(static_cast<int>(id) > previous[producer]);
		previous[producer] = id;
	}
	assert(delivered == accepted);
	printf("PASS: four concurrent producers, %u accepted callbacks delivered exactly once in producer order\n", handled);

	reset();
	delete eventQueue;
	eventQueue = nullptr;
	printf("PASS: 14 deterministic scenarios; 1000000 seeded scheduling steps, %llu events delivered exactly once\n",
		   static_cast<unsigned long long>(totalAccepted));
}
