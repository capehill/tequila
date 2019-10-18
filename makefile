NAME = Tequila

CC = gcc
CFLAGS = -Wall -Wextra -O3 -gstabs
AMIGADATE = $(shell date LFORMAT "%-d.%-m.%Y")
OBJS = profiler.o timer.o common.o

all: $(NAME)

%.o : %.c
	$(CC) -c $< $(CFLAGS) -D__AMIGA_DATE__="$(AMIGADATE)"

$(NAME): $(OBJS)
	$(CC) -o $@ $(OBJS)

strip:
	strip $(NAME)

clean:
	delete $(NAME)
	delete #?.o