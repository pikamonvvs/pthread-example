all:
	gcc -o main.exe main.c -pthread
	.\main.exe

clean:
	rm -f main.exe
