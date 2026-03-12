wheelswipe: main.c
	gcc -o wheelswipe main.c

clean:
	rm wheelswipe

format:
	astyle main.c
