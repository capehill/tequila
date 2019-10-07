NAME = tequila

all: $(NAME)

profiler.o: profiler.c
	gcc -c $< -Wall -Wextra -O3 -gstabs

$(NAME): profiler.o
	gcc -o $@ $<

strip:
	strip $(NAME)


