#pragma once
using tcpip_callback_fn = void (*)(void*);
struct tcpip_callback_msg {
	tcpip_callback_fn callback;
	void* arg;
};
constexpr int ERR_OK = 0;
constexpr int ERR_MEM = -1;
constexpr int ERR_NEED_SCHED = 123;
inline unsigned rejectCount;
inline bool needsScheduling;
inline bool failCallbackAllocation;
inline std::function<void()> beforeMailboxPost;
inline std::deque<tcpip_callback_msg*> mailbox;
inline std::mutex mailboxMutex;
inline tcpip_callback_msg* tcpip_callbackmsg_new(tcpip_callback_fn callback, void* arg)
{
	if(failCallbackAllocation) return nullptr;
	return new tcpip_callback_msg{callback, arg};
}
inline int tcpip_callbackmsg_trycallback_fromisr(tcpip_callback_msg* message)
{
	if(beforeMailboxPost) {
		auto callback = std::move(beforeMailboxPost);
		beforeMailboxPost = {};
		callback();
	}
	std::lock_guard<std::mutex> guard(mailboxMutex);
	if(rejectCount) {
		--rejectCount;
		return ERR_MEM;
	}
	assert(mailbox.empty());
	mailbox.push_back(message);
	return needsScheduling ? ERR_NEED_SCHED : ERR_OK;
}
