fuzz-build-record-layer: fuzz-prepare
	go-fuzz-build -tags gofuzz -func FuzzRecordLayer
fuzz-run-record-layer:
	go-fuzz -bin dtls-fuzz.zip -workdir fuzz
fuzz-prepare:
	@GO111MODULE=on go mod vendor
