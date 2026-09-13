# Testable Code and Test-Driven Development

**Scope:** The single owner of how tests are added to SRS and Oryx — the test-first contract, the definition of mockable, and the C++ patterns that make mocking possible. Every workflow that writes or changes a test loads this file; do not restate these rules elsewhere.

**The principle behind every rule below:** code that cannot be mocked cannot be unit tested. When a test is hard to write, the test is reporting a defect in the code's structure, not a reason to skip the test.

## The test-first contract

Apply this whenever a task adds a test, regardless of which task the router selected.

1. **Write the test before the implementation.** The test states the intended behavior — the goal — not the behavior that exists today.
2. **Run it and watch it fail.** A red-phase test that was never executed proves nothing. Confirm it fails, and that it fails for the reason you intended rather than a setup mistake, a crash, or a compile error.
3. **Never implement the behavior in the same step as the test.** Writing the test and making it pass are separate, reviewable steps. Report the observed failure to the maintainer before implementing anything.
4. **Report the exact red/green split.** Name which tests fail, which pass, and the assertion output for each. Do not describe a test as failing "as designed" without having run it.

A bug fix and a new capability both start red: a bug's regression test fails until the fix lands, and a capability's test fails until the capability exists.

**The one exception:** a test that locks in an accepted limitation passes from the start, because current behavior is the intended final behavior. Say so explicitly in the test's comment so a future reader does not mistake it for a test that should have been red. Example: MPEG-TS over SRT carries no application status channel, so "a rejected viewer is refused with no reason delivered" is the permanent, correct behavior.

## The testability contract

A class is mockable when all three hold. Check them before writing the test, not after.

1. **Every collaborator is reached through an interface.** A member typed as a concrete class (`SrsSecurity *security_`) cannot accept a mock. Type it as the interface (`ISrsSecurity *security_`).
2. **No global is dereferenced outside the constructor.** `_srs_config->get_x()` inside a method body is untestable. The global may appear exactly once per dependency, as a constructor capture.
3. **No constructor calls a function.** A constructor that calls `subscribe()`, reads config, or starts a timer runs that work before a test can inject anything.

**Refactoring to satisfy these three rules is not a feature.** It is permitted in C++ maintenance mode, and it is required when a test needs it. It must not change behavior: same calls, same order, same effects — only the path by which a dependency is reached changes. If a testability refactor would alter behavior, stop and raise it with the maintainer instead.

## C++ patterns

C++ has no fake-generation framework, so mockability is achieved structurally. These patterns are already used throughout `trunk/src/app/`; follow them rather than inventing a new approach.

### Interface and implementation

Declare the contract as `ISrsXxx` with pure virtuals, implement it as `SrsXxx`, and have every consumer depend on the interface. `ISrsHttpHooks`/`SrsHttpHooks` and `ISrsRtmpServer`/`SrsRtmpServer` are representative.

Before adding a member, confirm the interface already declares every method the consumer calls. Extending the interface is legitimate when a method is missing, but check first — it usually is not.

### Capture globals in the constructor

```cpp
SrsLiveStream::SrsLiveStream(ISrsRequest *r, ISrsBufferCache *c)
{
    security_ = new SrsSecurity();

    config_ = _srs_config;
    live_sources_ = _srs_sources;
    stat_ = _srs_stat;
    hooks_ = _srs_hooks;
}
```

Method bodies then use `config_`, `stat_`, `hooks_` only. The established member names are `config_`, `stat_`, `hooks_`, `live_sources_`, `security_`, and `shared_timer_`; reuse them instead of coining new ones.

### `assemble()` — no function calls in constructors

Work that must happen at construction but calls into a collaborator goes into `assemble()`, which the owner calls immediately after constructing the object:

```cpp
void SrsHlsStream::assemble()
{
    shared_timer_->timer5s()->subscribe(this);
}
```

Declare it with the conventional comment: `void assemble(); // Construct object, to avoid call function in constructor.`

Every construction site must call it — find them all before making the change. When the object is a by-value member of another class, that owner needs its own `assemble()` that forwards down the chain, and the owner's construction sites must call that.

This is what makes a subscription mockable: the test constructs, injects a mock, then calls `assemble()`, so the subscription lands on the mock and the destructor unsubscribes from that same mock. Leaving the call in the constructor means subscribing to the real global and unsubscribing from the mock, which leaves a dangling pointer in the real collaborator.

### Destructors

Unsubscribe through the member, guard against a null injected dependency, and null the injected members:

```cpp
SrsHlsStream::~SrsHlsStream()
{
    if (shared_timer_) {
        shared_timer_->timer5s()->unsubscribe(this);
    }
    ...
    config_ = NULL;
    stat_ = NULL;
    hooks_ = NULL;
    shared_timer_ = NULL;
}
```

### Reaching private members from a test

`SRS_DECLARE_PRIVATE` expands to `public` in utest builds and `private` otherwise (`trunk/src/core/srs_core.hpp`). Tests therefore inject and assert on private members directly; no friend declarations or accessors are needed.

### Injecting and tearing down in a test

Ownership is the trap. A member the class allocates is a member the class frees, so replace it with `srs_freep()` first and null it before the object is destroyed:

```cpp
stream.config_ = &config;          // captured global, not owned: null it at the end
srs_freep(stream.security_);       // allocated by the constructor: free, then replace
stream.security_ = &security;
...
stream.config_ = NULL;             // prevent a double free
stream.security_ = NULL;
```

Skipping the teardown nulls produces a double free that surfaces as an unrelated crash under the sanitizer build.

### Where mocks live and how they are named

Mocks are declared in a `srs_utest_*.hpp` and defined in the matching `.cpp`. Shared, widely reused mocks live in `srs_utest_manual_mock.hpp`. Name them `Mock<Collaborator>For<Context>` — `MockHttpHooksForLiveStream`, `MockAppConfigForHttpHooksOnPlay` — and search for an existing mock before writing a new one; most collaborators already have one.

Mocks record what they were asked to do (`on_play_count_`, `start_play_count_`, `on_play_calls_`) and let the test choose the outcome (`on_play_error_`, `start_play_error_`). Extend an existing mock with a new recording field rather than duplicating it.

### Registering a new utest file

Add the basename to the `MODULE_FILES` list in `trunk/configure`, then re-run `./configure --utest`; a new file is not compiled until configure regenerates the makefile.

Some test files are conditional: `srs_utest_ai21`/`ai22` build only with `--rtsp=on` (default off) and `srs_utest_ai23` only with GB28181. A test that includes an RTSP or GB28181 header must live in one of those files, not in an unconditionally built one.

Build and run:

```bash
cd trunk && ./configure --utest && make utest && ./objs/srs_utest
./objs/srs_utest --gtest_filter=SuiteName.*
```

Reconfiguring overwrites the existing build configuration. Ask the maintainer for the flags they want — notably `--sanitizer=on` and `--rtsp=on` — before re-running configure on their working tree.

## Go

The Go proxy and Oryx get this for free: depend on interfaces and let counterfeiter generate the fakes. After changing or adding an interface carrying a `//go:generate go tool counterfeiter ...` directive, regenerate with `make generate`. The test-first contract above applies unchanged.

## Before writing a test

- Does a mock already exist for each collaborator?
- Does every collaborator the test must control sit behind an interface member?
- Does any method body dereference a global?
- Does the constructor call a function?
- If any answer forces a refactor: is it behavior-preserving, and have you found every construction site?
- Does the test assert the goal rather than current behavior, and have you run it and seen it fail?
