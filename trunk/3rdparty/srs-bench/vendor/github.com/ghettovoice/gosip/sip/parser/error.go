package parser

type Error interface {
	error
	// Syntax indicates that this is syntax error
	Syntax() bool
}

type InvalidStartLineError string

func (err InvalidStartLineError) Syntax() bool    { return true }
func (err InvalidStartLineError) Malformed() bool { return false }
func (err InvalidStartLineError) Broken() bool    { return true }
func (err InvalidStartLineError) Error() string   { return "parser.InvalidStartLineError: " + string(err) }

type InvalidMessageFormat string

func (err InvalidMessageFormat) Syntax() bool    { return true }
func (err InvalidMessageFormat) Malformed() bool { return true }
func (err InvalidMessageFormat) Broken() bool    { return true }
func (err InvalidMessageFormat) Error() string   { return "parser.InvalidMessageFormat: " + string(err) }

type WriteError string

func (err WriteError) Syntax() bool  { return false }
func (err WriteError) Error() string { return "parser.WriteError: " + string(err) }
