CC = gcc
CFLAGS = -Wall -g

HEADERS = list.h queue.h shared.h

OSS = oss
OSS_SRC = oss.c
OSS_OBJ = $(OSS_SRC:.c=.o) list.o queue.o

USER = user
USER_SRC = user.c
USER_OBJ = $(USER_SRC:.c=.o)

OUTPUT = $(OSS) $(USER)

.PHONY: all clean

all: $(OUTPUT)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OSS): $(OSS_OBJ)
	$(CC) $(CFLAGS) $(OSS_OBJ) -o $(OSS)

$(USER): $(USER_OBJ)
	$(CC) $(CFLAGS) $(USER_OBJ) -o $(USER)

clean:
	/bin/rm -f $(OUTPUT) *.o *.log