grizzly: grizzly.c
	gcc grizzly.c -o grizzly -Wall -std=c99

format:
	clang-format -i grizzly.c

clean:
	rm grizzly
