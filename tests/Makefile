# Define a common prefix where binaries and docs install
PREFIX = /usr
sbindir = bin

CC = gcc
CFLAGS += -g -ggdb -Wall
LDFLAGS = -libverbs -lgf_complete -lJerasure -lpthread -lrdmacm

OBJECTS = ec_test_env.o
TARGETS = ec_test_env 

all: $(TARGETS)

ibv_ec_capability_test: ec_capability_test.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ec_test_env: ec_test_env.o
	$(CC) $(CFLAGS) -o $@ $^ -libverbs

install:
	install -d -m 755 $(PREFIX)/$(sbindir)
	install -m 755 $(TARGETS) $(PREFIX)/$(sbindir)
clean:
	rm -f $(OBJECTS) $(TARGETS)
