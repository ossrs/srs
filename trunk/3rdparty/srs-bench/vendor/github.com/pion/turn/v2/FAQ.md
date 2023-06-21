

## FAQ

Q: Will pion/turn also act as a STUN server?

A: Yes.

Q: How do I implement token-based authentication?

A: Replace the username with a token in the [AuthHandler](https://github.com/pion/turn/blob/6d0ff435910870eb9024b18321b93b61844fcfec/examples/turn-server/simple/main.go#L49).
The password sent by the client can be any non-empty string, as long as it matches that used by the [GenerateAuthKey](https://github.com/pion/turn/blob/6d0ff435910870eb9024b18321b93b61844fcfec/examples/turn-server/simple/main.go#L41)
function.

Q: Will WebRTC prioritize using STUN over TURN?

A: Yes.