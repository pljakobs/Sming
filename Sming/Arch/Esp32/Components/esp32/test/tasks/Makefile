CXX ?= c++
TASKS_SOURCE ?= $(abspath ../../src/tasks.cpp)
BUILD_DIR ?= /tmp/sming-esp32-task-tests
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -pthread -fsanitize=undefined -fsanitize-undefined-trap-on-error

.PHONY: test
test:
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -I../../src -DTASKS_SOURCE='"$(TASKS_SOURCE)"' test.cpp -o $(BUILD_DIR)/test
	$(BUILD_DIR)/test
	$(CXX) $(CXXFLAGS) -O2 -DTEST_RELEASE -Iinclude -I../../src -DTASKS_SOURCE='"$(TASKS_SOURCE)"' test.cpp -o $(BUILD_DIR)/test-release
	$(BUILD_DIR)/test-release
	$(CXX) $(CXXFLAGS) -DDISABLE_NETWORK -Iinclude -I../../src -fsyntax-only $(TASKS_SOURCE)
