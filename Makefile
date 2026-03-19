wheelswipe: main.c
	gcc -Wall -Wextra -o wheelswipe main.c

clean:
	rm wheelswipe

format:
	astyle main.c
