.PHONY: help default clean signaling

default: signaling

clean:
	rm -f ./objs/signaling

.format.txt: *.go
	gofmt -w .
	echo "done" > .format.txt

signaling: ./objs/signaling

./objs/signaling: .format.txt *.go Makefile
	go build -mod=vendor -o objs/signaling .

help:
	@echo "Usage: make [signaling]"
	@echo "     signaling       Make the signaling to ./objs/signaling"
