NAME=logrus-prefixed-formatter
PACKAGES=$(shell go list ./...)

deps:
	@echo "--> Installing dependencies"
	@go get -d -v -t ./...

test-deps:
	@which ginkgo 2>/dev/null ; if [ $$? -eq 1 ]; then \
		go get -u -v github.com/onsi/ginkgo/ginkgo; \
	fi

test: test-deps
	@echo "--> Running tests"
	@ginkgo -r --randomizeAllSpecs --randomizeSuites --failOnPending --cover --trace --race

format:
	@echo "--> Running go fmt"
	@go fmt $(PACKAGES)
