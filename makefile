NAME = tequila

CC = gcc
CFLAGS = -Wall -Wextra -O3 -gstabs

OBJS = profiler.o timer.o common.o

all: $(NAME)

%.o : %.c
	$(CC) -c $< $(CFLAGS)

$(NAME): $(OBJS)
	$(CC) -o $@ $(OBJS)

strip:
	strip $(NAME)

clean:
	delete $(NAME)
	delete #?.o