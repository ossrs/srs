# Testable Code and Test-Driven Development

**Scope:** The single owner of how tests are added to SRS and Oryx and how they are run — the test-first contract, the test platforms, running tests in a subagent, the definition of mockable, and the C++ patterns that make mocking possible. Every task that touches code or runs tests loads this file; do not restate these rules elsewhere.

**The principle behind every rule below:** code that cannot be mocked cannot be unit tested. When a test is hard to write, the test is reporting a defect in the code's structure, not a reason to skip the test.

## The test-first contract

Apply this to every task that touches code, regardless of which task the router selected. Work proceeds in three steps, in order, never merged.

1. **Tests first, no implementation.** Write every test that verifies the issue, the feature, or the interface. Start with integration tests through existing public APIs, such as srs-bench and the workflow utests, because they need no new interface. Then write unit tests, declaring any new interface as stubs with no behavior so they compile. Run them all. Failing is the expected result.
2. **Refactor for testability, no implementation.** When the code under test cannot be mocked, refactor it under the testability contract below. Tests may still fail.
3. **Implement, then make every test pass.** Fix the bug or build the feature. Edit a test in this step only to fix a proven mistake in the test itself, and say so.

Rules that hold across the steps:

- **The test states the goal**, the intended behavior, not the behavior that exists today.
- **Run it and watch it fail.** A red-phase test that was never executed proves nothing. Confirm it fails, and that it fails for the reason you intended rather than a setup mistake, a crash, or a compile error.
- **Report the observed failure to the maintainer before step 3.** Writing the tests and making them pass are separate, reviewable steps.
- **Report the exact red/green split.** Name which tests fail, which pass, and the assertion output for each. Do not describe a test as failing "as designed" without having run it.

A bug fix and a new capability both start red: a bug's regression test fails until the fix lands, and a capability's test fails until the capability exists.

**The one exception:** a test that locks in an accepted limitation passes from the start, because current behavior is the intended final behavior. Say so explicitly in the test's comment so a future reader does not mistake it for a test that should have been red. Example: MPEG-TS over SRT carries no application status channel, so "a rejected viewer is refused with no reason delivered" is the permanent, correct behavior.

## Tests come from scenarios, not lines

⚠️ **Never write a test just to run a line of code.** Each line serves a scenario: a protocol workflow, a feature, a client or peer behavior, a config, or an error from the network or the OS. Coverage is the result of testing scenarios, never the goal of a test. Before writing any test:

- Work out which scenario reaches the code, and why the code handles it the way it does.
- Write the test as that scenario: set up what a real client, peer or config does, and assert what the scenario must produce.
- An edge case is fine when it is a real one, such as a truncated packet or a peer that disconnects mid-handshake; name it in the test.
- If no real scenario reaches the code, do not cover it; report it as possibly dead code.

## Test platforms

Run the tests on every platform the change can reach, and report the results per OS.

- Detect the platforms, never assume them: the local OS from `uname -s` and `uname -m`, Linux in Docker when `docker info` succeeds, and the remote hosts the routed code map describes.
- Report a table with one row per OS and CPU, such as macOS arm64, Linux arm64 (Docker), and Windows x64, and the passed, failed, and skipped counts of each test type.
- List every OS that was not tested and why, such as Docker not running or no Windows host set up.

## Run tests in a subagent

Start tests in a subagent, so the main agent's context window stays small.

- The subagent runs the test commands and reads their logs.
- It returns only a short report: the lock lines the script prints, the table of test platforms above, each failure with its key log lines, and any blocker.
- For a Gate or Full tier, it returns the report in the Sub-agent section of `references/integration-tests.md`.
- A subagent that already runs a task, such as an `srs-autopilot` task, runs its tests itself.

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

AI maintains the `srs_utest_ai*` and `srs_utest_workflow_*` files: a unit test goes into an existing `srs_utest_ai*` file or a new `srs_utest_aiNN`, a workflow test into the matching `srs_utest_workflow_*` file. The `srs_utest_manual_*` files are defined and implemented by humans; never add a test to them.

Add the basename to `SRS_UTEST_FILES` in `trunk/auto/module_files.sh`; a new file is not compiled until configure regenerates the makefile, which `srs-utest-run.sh` does on its own.

Some test files are conditional: `srs_utest_ai21`/`ai22` build only with `--rtsp=on` (default off) and `srs_utest_ai23` only with GB28181. A test that includes an RTSP or GB28181 header must live in one of those files, not in an unconditionally built one.

Build and run:

```bash
bash skills/srs-develop/scripts/srs-utest-run.sh
bash skills/srs-develop/scripts/srs-utest-run.sh 'SuiteName.*'
```

Reconfiguring overwrites the existing build configuration. Ask the maintainer for the flags they want — notably `--sanitizer=on` and `--rtsp=on` — before re-running configure on their working tree.

The same tests build as `srs_utest.exe` on Windows, so a test must not assume a Unix host:

- Put temporary files in `srs_utest_tmp_dir()`, not `/tmp`, and use `SRS_UTEST_NULL_FILE` for `/dev/null`.
- Guard a test that cannot run on Windows as `references/cpp-code-style.md` Platform Calls says.
- When the change reaches Windows, run it there as `references/develop-code.md` C++ Step 4 says.

## Go

The Go proxy gets this for free: depend on interfaces and let counterfeiter generate the fakes. After changing or adding an interface carrying a `//go:generate go tool counterfeiter ...` directive, regenerate with `make generate`. Oryx has no generated fakes. The test-first contract above applies unchanged to both.

## Before writing a test

- Does a mock already exist for each collaborator?
- Does every collaborator the test must control sit behind an interface member?
- Does any method body dereference a global?
- Does the constructor call a function?
- If any answer forces a refactor: is it behavior-preserving, and have you found every construction site?
- Does the test assert the goal rather than current behavior, and have you run it and seen it fail?
