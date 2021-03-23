package ice

import "github.com/pion/randutil"

const (
	runesAlpha                 = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
	runesDigit                 = "0123456789"
	runesCandidateIDFoundation = runesAlpha + runesDigit + "+/"

	lenUFrag = 16
	lenPwd   = 32
)

// Seeding random generator each time limits number of generated sequence to 31-bits,
// and causes collision on low time accuracy environments.
// Use global random generator seeded by crypto grade random.
var (
	globalMathRandomGenerator  = randutil.NewMathRandomGenerator()               //nolint:gochecknoglobals
	globalCandidateIDGenerator = candidateIDGenerator{globalMathRandomGenerator} //nolint:gochecknoglobals
)

// candidateIDGenerator is a random candidate ID generator.
// Candidate ID is used in SDP and always shared to the other peer.
// It doesn't require cryptographic random.
type candidateIDGenerator struct {
	randutil.MathRandomGenerator
}

func newCandidateIDGenerator() *candidateIDGenerator {
	return &candidateIDGenerator{
		randutil.NewMathRandomGenerator(),
	}
}

func (g *candidateIDGenerator) Generate() string {
	// https://tools.ietf.org/html/rfc5245#section-15.1
	// candidate-id = "candidate" ":" foundation
	// foundation   = 1*32ice-char
	// ice-char     = ALPHA / DIGIT / "+" / "/"
	return "candidate:" + g.MathRandomGenerator.GenerateString(32, runesCandidateIDFoundation)
}

// generatePwd generates ICE pwd.
// This internally uses generateCryptoRandomString.
func generatePwd() (string, error) {
	return randutil.GenerateCryptoRandomString(lenPwd, runesAlpha)
}

// generateUFrag generates ICE user fragment.
// This internally uses generateCryptoRandomString.
func generateUFrag() (string, error) {
	return randutil.GenerateCryptoRandomString(lenUFrag, runesAlpha)
}
