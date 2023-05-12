#!/usr/bin/make

DBG ?= 0

ifeq ($(shell uname -s), Darwin)
	PKG_CONFIG_PATH = $(shell brew --prefix openssl)/lib/pkgconfig
endif

CXX	  = g++
LD	  = $(CXX)
LIBS	  = openssl
CXXFLAGS  = -std=gnu++20 -DFMT_HEADER_ONLY=1
CXXFLAGS += $(shell env PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
		pkg-config --cflags $(LIBS))
LDFLAGS   = $(shell env PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
		pkg-config --libs $(LIBS))

ifeq ($(strip $(DBG)), 1)
	CXXFLAGS += -g -Og #-fsanitize=address
	#LDFLAGS  += -fsanitize=address
else
	CXXFLAGS += -O2 -DNDEBUG
endif

SERVER_EXE = pa02_server
CLIENT_EXE = pa02_client

all: compile

compile: $(SERVER_EXE) $(CLIENT_EXE)

server: $(SERVER_EXE)
	./$(SERVER_EXE)

client1: $(CLIENT_EXE)
	./$(CLIENT_EXE) 1

client2: $(CLIENT_EXE)
	./$(CLIENT_EXE) 2

client3: $(CLIENT_EXE)
	./$(CLIENT_EXE) 3

$(SERVER_EXE): server.o blockchain.o client_connection.o
	$(LD) $^ $(LDFLAGS) -o $@

$(CLIENT_EXE): client.o
	$(LD) $^ $(LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $^ -c

clean:
	rm -f $(SERVER_EXE) $(CLIENT_EXE) *.o

.PHONY: all clean compile server client1 client2 client3
