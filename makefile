NAME = tequila

CC = gcc
CFLAGS = -Wall -Wextra -O3 -gstabs

OBJS = profiler.o timer.o

all: $(NAME)

%.o : %.c
	$(CC) -c $< $(CFLAGS)

#profiler.o: profiler.c
#	 gcc -c $< $(CFLAGS)

#timer.o: timer.c
#    gcc -c $< $(CFLAGS)

$(NAME): $(OBJS)
	$(CC) -o $@ $(OBJS)

strip:
	strip $(NAME)

clean:
	delete $(NAME)
	delete #?.o