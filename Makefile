main:main.c
	gcc -Wall -Wextra main.c -o main -lpthread
clean:
	rm -rf main