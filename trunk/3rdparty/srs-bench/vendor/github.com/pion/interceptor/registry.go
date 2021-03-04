package interceptor

// Registry is a collector for interceptors.
type Registry struct {
	interceptors []Interceptor
}

// Add adds a new Interceptor to the registry.
func (i *Registry) Add(icpr Interceptor) {
	i.interceptors = append(i.interceptors, icpr)
}

// Build constructs a single Interceptor from a InterceptorRegistry
func (i *Registry) Build() Interceptor {
	if len(i.interceptors) == 0 {
		return &NoOp{}
	}

	return NewChain(i.interceptors)
}
