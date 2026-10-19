package main

import (
	"errors"
	"math"
)

// difference is new minus old with its two-sided 95% confidence interval.
// OldMean is the old side's mean over the samples used, for the relative change.
type difference struct {
	Method  string
	N       int
	DF      float64
	Mean    float64
	OldMean float64
	Low     float64
	High    float64
}

func (d difference) containsZero() bool {
	return d.Low <= 0 && d.High >= 0
}

func meanAndVariance(values []float64) (float64, float64) {
	s := statOf(values)
	return s.Mean, s.SD * s.SD
}

// pairedDifference is the mean of the per-second differences new[i] - old[i],
// for an A/B run whose two sides were sampled in the same seconds, so the noise
// both servers share cancels.
func pairedDifference(old, newer []float64) (difference, error) {
	if len(old) != len(newer) {
		return difference{}, errors.New("paired series differ in length")
	}
	if len(old) < 2 {
		return difference{}, errors.New("a difference needs at least 2 samples")
	}
	diffs := make([]float64, len(old))
	for i := range old {
		diffs[i] = newer[i] - old[i]
	}
	mean, variance := meanAndVariance(diffs)
	oldMean, _ := meanAndVariance(old)
	n := float64(len(diffs))
	half := studentT975(n-1) * math.Sqrt(variance/n)
	return difference{Method: "paired", N: len(diffs), DF: n - 1, Mean: mean, OldMean: oldMean, Low: mean - half, High: mean + half}, nil
}

// welchDifference is the difference of the means of two windows measured at
// different times, with Welch's degrees of freedom, which do not assume equal
// variances.
func welchDifference(old, newer []float64) (difference, error) {
	if len(old) < 2 || len(newer) < 2 {
		return difference{}, errors.New("a difference needs at least 2 samples on each side")
	}
	oldMean, oldVar := meanAndVariance(old)
	newMean, newVar := meanAndVariance(newer)
	a, b := oldVar/float64(len(old)), newVar/float64(len(newer))
	mean := newMean - oldMean
	se := math.Sqrt(a + b)
	df := float64(len(old) + len(newer) - 2)
	if a+b > 0 {
		df = (a + b) * (a + b) / (a*a/float64(len(old)-1) + b*b/float64(len(newer)-1))
	}
	half := studentT975(df) * se
	return difference{Method: "unpaired", N: len(old) + len(newer), DF: df, Mean: mean, OldMean: oldMean, Low: mean - half, High: mean + half}, nil
}

// pairBySecond pairs the SRS CPU of two A/B sides by wall-clock time: each old
// sample takes the nearest unused new sample within half a second. Each side
// samples on its own one-second ticker, so their seconds are offset by less
// than a second; a second with no partner is left out.
func pairBySecond(old, newer []sample) ([]float64, []float64) {
	var a, b []float64
	used := make([]bool, len(newer))
	for _, o := range old {
		best := -1
		var bestGap int64
		for i, n := range newer {
			gap := n.UnixMillis - o.UnixMillis
			if gap < 0 {
				gap = -gap
			}
			if used[i] || gap >= 500 || (best >= 0 && gap >= bestGap) {
				continue
			}
			best, bestGap = i, gap
		}
		if best < 0 {
			continue
		}
		used[best] = true
		a = append(a, o.SRSCPUPercent)
		b = append(b, newer[best].SRSCPUPercent)
	}
	return a, b
}

// studentT975 is the 0.975 quantile of Student's t with df degrees of freedom,
// the half-width factor of a two-sided 95% CI. df need not be an integer, for
// Welch's df.
func studentT975(df float64) float64 {
	low, high := 0.0, 1.0
	for studentTCDF(high, df) < 0.975 {
		high *= 2
	}
	for i := 0; i < 200; i++ {
		mid := (low + high) / 2
		if studentTCDF(mid, df) < 0.975 {
			low = mid
		} else {
			high = mid
		}
	}
	return (low + high) / 2
}

// studentTCDF is P(T <= t) for t >= 0: 1 - I_x(df/2, 1/2) / 2, x = df/(df+t^2).
func studentTCDF(t, df float64) float64 {
	return 1 - regularizedBeta(df/(df+t*t), df/2, 0.5)/2
}

// regularizedBeta is I_x(a, b), by the continued fraction of Numerical Recipes'
// betai, using the symmetry I_x(a, b) = 1 - I_{1-x}(b, a) where it converges
// faster.
func regularizedBeta(x, a, b float64) float64 {
	if x <= 0 {
		return 0
	}
	if x >= 1 {
		return 1
	}
	la, _ := math.Lgamma(a + b)
	lb, _ := math.Lgamma(a)
	lc, _ := math.Lgamma(b)
	front := math.Exp(la - lb - lc + a*math.Log(x) + b*math.Log(1-x))
	if x < (a+1)/(a+b+2) {
		return front * betaFraction(x, a, b) / a
	}
	return 1 - front*betaFraction(1-x, b, a)/b
}

// betaFraction evaluates the continued fraction by the modified Lentz method.
func betaFraction(x, a, b float64) float64 {
	const tiny = 1e-300
	c, d := 1.0, 1-(a+b)*x/(a+1)
	if math.Abs(d) < tiny {
		d = tiny
	}
	d = 1 / d
	h := d
	for m := 1; m <= 1000; m++ {
		fm := float64(m)
		aa := fm * (b - fm) * x / ((a + 2*fm - 1) * (a + 2*fm))
		for i := 0; i < 2; i++ {
			d = 1 + aa*d
			if math.Abs(d) < tiny {
				d = tiny
			}
			c = 1 + aa/c
			if math.Abs(c) < tiny {
				c = tiny
			}
			d = 1 / d
			h *= d * c
			aa = -(a + fm) * (a + b + fm) * x / ((a + 2*fm) * (a + 2*fm + 1))
		}
		if math.Abs(d*c-1) < 1e-15 {
			break
		}
	}
	return h
}
