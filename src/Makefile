EXEC = tema2

SRCS = tema2.c peer.c tracker.c download.c
OBJS = $(SRCS:.c=.o)

CC = mpicc
CFLAGS = -Wall -pthread

all: build

build: $(OBJS)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXEC)
