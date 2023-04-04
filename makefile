NAME = Tequila

CC = ppc-amigaos-gcc
CFLAGS = -Wall -Wextra -O3 -gstabs
AMIGADATE = $(shell date LFORMAT "%-d.%-m.%Y")
OBJS = profiler.o timer.o common.o symbols.o gui.o main.o
DEPS = $(OBJS:.o=.d)

all: $(NAME)

%.o : %.c
	$(CC) -c $< $(CFLAGS) -D__AMIGA_DATE__="$(AMIGADATE)"

$(NAME): $(OBJS)
	$(CC) -o $@ $(OBJS) -lauto

strip:
	ppc-amigaos-strip $(NAME)

# Dependencies
%.d : %.c
	$(CC) -MM -MP -MT $(@:.d=.o) -o $@ $< $(CFLAGS)

clean:
	rm $(NAME)
	rm *.o

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

