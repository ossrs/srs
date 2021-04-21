package turn

import ( //nolint:gci
	"crypto/hmac"
	"crypto/sha1" //nolint:gosec,gci
	"encoding/base64"
	"net"
	"strconv"
	"time"

	"github.com/pion/logging"
)

// GenerateCredentials can be used to create credentials valid for [duration] time
func GenerateLongTermCredentials(sharedSecret string, duration time.Duration) (string, string, error) {
	t := time.Now().Add(duration).Unix()
	username := strconv.FormatInt(t, 10)
	password, err := longTermCredentials(username, sharedSecret)
	return username, password, err
}

func longTermCredentials(username string, sharedSecret string) (string, error) {
	mac := hmac.New(sha1.New, []byte(sharedSecret))
	_, err := mac.Write([]byte(username))
	if err != nil {
		return "", err // Not sure if this will ever happen
	}
	password := mac.Sum(nil)
	return base64.StdEncoding.EncodeToString(password), nil
}

// NewAuthHandler returns a turn.AuthAuthHandler used with Long Term (or Time Windowed) Credentials.
// https://tools.ietf.org/search/rfc5389#section-10.2
func NewLongTermAuthHandler(sharedSecret string, l logging.LeveledLogger) AuthHandler {
	if l == nil {
		l = logging.NewDefaultLoggerFactory().NewLogger("turn")
	}
	return func(username, realm string, srcAddr net.Addr) (key []byte, ok bool) {
		l.Tracef("Authentication username=%q realm=%q srcAddr=%v\n", username, realm, srcAddr)
		t, err := strconv.Atoi(username)
		if err != nil {
			l.Errorf("Invalid time-windowed username %q", username)
			return nil, false
		}
		if int64(t) < time.Now().Unix() {
			l.Errorf("Expired time-windowed username %q", username)
			return nil, false
		}
		password, err := longTermCredentials(username, sharedSecret)
		if err != nil {
			l.Error(err.Error())
			return nil, false
		}
		return GenerateAuthKey(username, realm, password), true
	}
}
