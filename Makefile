default:
	mkdir -p bin
	cc loto.c -o bin/loto

install: default
	cp bin/loto ~/bin/loto

clean:
	rm -r bin
