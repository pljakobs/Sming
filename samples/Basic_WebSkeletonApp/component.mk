ARDUINO_LIBRARIES := ArduinoJson6
HWCONFIG := spiffs-2m

# Use to store files in a FlashString map object instead of SPIFFS
CONFIG_VARS += ENABLE_FLASHSTRING_MAP
ENABLE_FLASHSTRING_MAP ?= 0
ifeq ($(ENABLE_FLASHSTRING_MAP),1)
COMPONENT_CXXFLAGS += -DENABLE_FLASHSTRING_MAP=1
endif
