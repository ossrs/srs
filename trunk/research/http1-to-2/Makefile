.PHONY: default clean

default: ./http1-to-2

./http1-to-2: *.go *.mod
	go build -o ./http1-to-2 .

clean:
	rm -f ./http1-to-2
