DOCKER_IMAGE ?= yamlio/libyaml-dev

build:
	docker build -t $(DOCKER_IMAGE) .

run: build
	docker run -it --rm $(DOCKER_IMAGE) bash

prepare-git:
	rm -rf output/libyaml.git
	git clone ../../.git output/libyaml.git

libyaml-dist: libyaml-dist-ci

libyaml-dist-ci: prepare-git
	docker run --rm -u $$(id -u) \
	  -v"$$PWD/output:/output" \
	  -v"$$PWD/scripts:/scripts" \
	  $(DOCKER_IMAGE) /scripts/libyaml-dist.sh

clean:
	rm -rf output/libyaml.git
	rm -rf output/yaml-*.*
