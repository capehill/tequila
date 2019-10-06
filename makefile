NAME = tequila

all: $(NAME)

profiler.o: profiler.c
	gcc -c $< -Wall -Wextra -O3 -gstabs

$(NAME): profiler.o
	gcc -o $@ $< -athread=native

strip:
	strip $(NAME)


