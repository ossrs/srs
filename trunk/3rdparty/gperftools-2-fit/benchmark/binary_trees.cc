// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
// Copied from
// http://benchmarksgame.alioth.debian.org/u64q/program.php?test=binarytrees&lang=gpp&id=2
// and slightly modified (particularly by adding multi-threaded
// operation to hit malloc harder).
//
// This version of binary trees is mostly new/delete benchmark
//
// NOTE: copyright of this code is unclear, but we only distribute
// source.

/* The Computer Language Benchmarks Game
 * http://benchmarksgame.alioth.debian.org/
 *
 * Contributed by Jon Harrop
 * Modified by Alex Mizrahi
 * Adapted for gperftools and added threads by Aliaksei Kandratsenka
 */

#include <algorithm>
#include <errno.h>
#include <iostream>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

struct Node {
  Node *l, *r;
  int i;
  Node(int i2) : l(0), r(0), i(i2) {}
  Node(Node *l2, int i2, Node *r2) : l(l2), r(r2), i(i2) {}
  ~Node() { delete l; delete r; }
  int check() const {
    if (l) {
      return l->check() + i - r->check();
    } else {
      return i;
    }
  }
};

Node *make(int i, int d) {
  if (d == 0) return new Node(i);
  return new Node(make(2*i-1, d-1), i, make(2*i, d-1));
}

void run(int given_depth) {
  int min_depth = 4,
    max_depth = std::max(min_depth+2,
			 given_depth),
    stretch_depth = max_depth+1;

  {
    Node *c = make(0, stretch_depth);
    std::cout << "stretch tree of depth " << stretch_depth << "\t "
      << "check: " << c->check() << std::endl;
    delete c;
  }

  Node *long_lived_tree=make(0, max_depth);

  for (int d=min_depth; d<=max_depth; d+=2) {
    int iterations = 1 << (max_depth - d + min_depth), c=0;
    for (int i=1; i<=iterations; ++i) {
      Node *a = make(i, d), *b = make(-i, d);
      c += a->check() + b->check();
      delete a;
      delete b;
    }
    std::cout << (2*iterations) << "\t trees of depth " << d << "\t "
	      << "check: " << c << std::endl;
  }

  std::cout << "long lived tree of depth " << max_depth << "\t "
	    << "check: " << (long_lived_tree->check()) << "\n";

  delete long_lived_tree;
}

static void *run_tramp(void *_a) {
  intptr_t a = reinterpret_cast<intptr_t>(_a);
  run(a);
  return 0;
}

int main(int argc, char *argv[]) {
  int given_depth = argc >= 2 ? atoi(argv[1]) : 20;
  int thread_count = std::max(1, argc >= 3 ? atoi(argv[2]) : 1) - 1;
  std::vector<pthread_t> threads(thread_count);

  for (int i = 0; i < thread_count; i++) {
    int rv = pthread_create(&threads[i], NULL,
                            run_tramp,
                            reinterpret_cast<void *>(given_depth));
    if (rv) {
      errno = rv;
      perror("pthread_create");
    }
  }
  run_tramp(reinterpret_cast<void *>(given_depth));
  for (int i = 0; i < thread_count; i++) {
    pthread_join(threads[i], NULL);
  }
  return 0;
}
