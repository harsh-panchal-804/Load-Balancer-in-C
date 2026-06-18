main:main.c
	gcc -Wall -Wextra main.c -o main -lpthread -lssl -lcrypto
clean:
	rm -rf main
