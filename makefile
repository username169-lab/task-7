CC = gcc
CFLAGS += -Wall -g -I. -MMD -MP
LDFLAGS =
EXECUTABLE = main
SRC_FILES = main.c
OBJ_FILES = $(SRC_FILES:.c=.o)
DEP_FILES = $(SRC_FILES:.c=.d)

.PHONY: all clean

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJ_FILES)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f $(OBJ_FILES) $(EXECUTABLE) $(DEP_FILES)

-include $(DEP_FILES)
