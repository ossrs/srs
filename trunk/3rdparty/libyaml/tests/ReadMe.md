# Testing the Parser and Emitter

There are several programs to test the parser and emitter.

## Parser

    echo 'foo: bar' | ./tests/run-parser-test-suite

This will output the parsing events in yaml-test-suite format:

    +STR
    +DOC
    +MAP
    =VAL :foo
    =VAL :bar
    -MAP
    -DOC
    -STR

For flow style events, you have to enable it with the `--flow` option:

    echo '{ foo: bar }' | ./tests/run-parser-test-suite --flow keep

    ...
    +MAP {}
    ...

In the future, this will be the default.

You can also explicitly disable this style with `--flow off`, or output
flow style always, with `--flow on`.

## Emitter

run-emitter-test-suite takes yaml-test-suite event format and emits YAML.

    ./tests/run-parser-test-suite ... | ./tests/run-emitter-test-suite

## Options

* `--directive (1.1|1.2)`

  Prints a version directive before every document.

* `--flow on`

  Will emit the whole document in flow style.

* `--flow off`

  Will emit the whole document in block style.

* `--flow keep`

  Will emit block/flow style like in the original document.

Example:
```
% echo 'foo: [bar, {x: y}]' |
  ./tests/run-parser-test-suite --flow keep |
  ./tests/run-emitter-test-suite --flow keep
foo: [bar, {x: y}]
```
