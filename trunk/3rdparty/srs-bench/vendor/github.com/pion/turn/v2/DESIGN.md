# Why Pion TURN
TURN servers aren't exactly a hot technology, they are usually an after thought when building something. Most of the time
beginners build an interesting WebRTC application, but at the very end realize they need a TURN server. It is really frustrating when you
want to share your cool new project, only to realize you have to run another service.

Then you find yourself building from source, fighting with config files and making changes you don't fully understand. Pion TURN was born
hoping to solve these frustrations. These are the guiding principals/features that define pion-turn.

## Easy setup
simple-turn is a statically built TURN server, configured by environment variables. The entire install setup is 5 commands, on any platform!
The goal is that anyone should be able to run a TURN server on any platform.

## Integration first
pion-turn makes no assumptions about how you authenticate users, how you log, or even your topology! Instead of running a dedicated TURN server you
can inherit from github.com/pion/turn and set whatever logger you want.

## Embeddable
You can add this to an existing service. This means all your config files stay homogeneous instead of having the mismatch that makes it harder to manage your services.
For small setups it is usually an overkill to deploy dedicated TURN servers, this makes it easier to solve the problems you care about.

## Safe
Golang provides a great foundation to build safe network services. Especially when running a networked service that is highly concurrent bugs can be devastating.

## Readable
All network interaction is commented with a link to the spec. This makes learning and debugging easier, the TURN server was written to also serve as a guide for others.

## Tested
Every commit is tested via travis-ci Go provides fantastic facilities for testing, and more will be added as time goes on.

## Shared libraries
Every pion product is built using shared libraries, allowing others to build things using existing tested STUN and TURN tools.
