package ice

import (
	"fmt"

	"github.com/pion/stun"
)

func assertInboundUsername(m *stun.Message, expectedUsername string) error {
	var username stun.Username
	if err := username.GetFrom(m); err != nil {
		return err
	}
	if string(username) != expectedUsername {
		return fmt.Errorf("%w expected(%x) actual(%x)", errMismatchUsername, expectedUsername, string(username))
	}

	return nil
}

func assertInboundMessageIntegrity(m *stun.Message, key []byte) error {
	messageIntegrityAttr := stun.MessageIntegrity(key)
	return messageIntegrityAttr.Check(m)
}
