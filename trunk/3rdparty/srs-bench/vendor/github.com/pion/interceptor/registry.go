// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package interceptor

// Registry is a collector for interceptors.
type Registry struct {
	factories []Factory
}

// Add adds a new Interceptor to the registry.
func (r *Registry) Add(f Factory) {
	r.factories = append(r.factories, f)
}

// Build constructs a single Interceptor from a InterceptorRegistry
func (r *Registry) Build(id string) (Interceptor, error) {
	if len(r.factories) == 0 {
		return &NoOp{}, nil
	}

	interceptors := []Interceptor{}
	for _, f := range r.factories {
		i, err := f.NewInterceptor(id)
		if err != nil {
			return nil, err
		}

		interceptors = append(interceptors, i)
	}

	return NewChain(interceptors), nil
}
