package main

import (
	"math"
	"testing"
)

func near(t *testing.T, name string, got, want, tolerance float64) {
	t.Helper()
	if math.Abs(got-want) > tolerance {
		t.Errorf("%s = %.6f, want %.6f", name, got, want)
	}
}

// The two-sided 95% quantiles of Student's t, from the standard table.
func TestStudentT975(t *testing.T) {
	for _, tc := range []struct{ df, want float64 }{
		{1, 12.706205}, {2, 4.302653}, {4, 2.776445}, {8, 2.306004}, {9, 2.262157},
		{10, 2.228139}, {30, 2.042272}, {60, 2.000298}, {120, 1.979930}, {1e7, 1.959964},
	} {
		near(t, "t975", studentT975(tc.df), tc.want, 1e-5)
	}
}

// d = new - old = -1, -1, 0, -1, -2: mean -1, sd sqrt(0.5), se 0.316228,
// t(0.975, 4) 2.776445, so the CI is -1 +- 0.877989.
func TestPairedDifference(t *testing.T) {
	d, err := pairedDifference([]float64{10, 12, 11, 13, 14}, []float64{9, 11, 11, 12, 12})
	if err != nil {
		t.Fatal(err)
	}
	if d.Method != "paired" || d.N != 5 || d.DF != 4 {
		t.Fatalf("difference %+v", d)
	}
	near(t, "mean", d.Mean, -1, 1e-9)
	near(t, "old mean", d.OldMean, 12, 1e-9)
	near(t, "low", d.Low, -1.877989, 1e-5)
	near(t, "high", d.High, -0.122011, 1e-5)
}

// Equal sizes and variances: se sqrt(10/5 + 10/5) = 2, Welch df 8,
// t(0.975, 8) 2.306004, so the CI is 3 +- 4.612008.
func TestWelchDifference(t *testing.T) {
	d, err := welchDifference([]float64{10, 12, 14, 16, 18}, []float64{13, 15, 17, 19, 21})
	if err != nil {
		t.Fatal(err)
	}
	if d.Method != "unpaired" || d.N != 10 {
		t.Fatalf("difference %+v", d)
	}
	near(t, "df", d.DF, 8, 1e-9)
	near(t, "mean", d.Mean, 3, 1e-9)
	near(t, "low", d.Low, -1.612008, 1e-5)
	near(t, "high", d.High, 7.612008, 1e-5)
}

// Unequal variances: old var 4 (n 3), new var 10 (n 5); se^2 = 4/3 + 2,
// df = (10/3)^2 / ((4/3)^2/2 + 2^2/4) = 5.882353.
func TestWelchDegreesOfFreedom(t *testing.T) {
	d, err := welchDifference([]float64{10, 12, 14}, []float64{11, 13, 15, 17, 19})
	if err != nil {
		t.Fatal(err)
	}
	near(t, "df", d.DF, 5.882353, 1e-6)
	half := studentT975(d.DF) * math.Sqrt(10.0/3)
	near(t, "low", d.Low, 3-half, 1e-9)
	if !(studentT975(6) < studentT975(d.DF) && studentT975(d.DF) < studentT975(5)) {
		t.Fatalf("t at df %g is not between df 5 and 6", d.DF)
	}
}

// Two identical series differ by nothing, so the CI is the single point 0.
func TestDifferenceOfIdenticalSeries(t *testing.T) {
	d, err := pairedDifference([]float64{5, 6, 7}, []float64{5, 6, 7})
	if err != nil {
		t.Fatal(err)
	}
	if d.Mean != 0 || d.Low != 0 || d.High != 0 || !d.containsZero() {
		t.Fatalf("difference %+v", d)
	}
}

func TestDifferenceNeedsTwoSamples(t *testing.T) {
	if _, err := pairedDifference([]float64{1}, []float64{2}); err == nil {
		t.Fatal("paired difference of one sample accepted")
	}
	if _, err := pairedDifference([]float64{1, 2}, []float64{2}); err == nil {
		t.Fatal("paired difference of unequal series accepted")
	}
	if _, err := welchDifference([]float64{1, 2}, []float64{2}); err == nil {
		t.Fatal("unpaired difference with one new sample accepted")
	}
}

// A/B sides sample on their own one-second tickers, so a second pairs with the
// other side's sample nearest in time, within half a second; a second with no
// partner is left out.
func TestPairBySecond(t *testing.T) {
	old := []sample{{UnixMillis: 1000, SRSCPUPercent: 10}, {UnixMillis: 2000, SRSCPUPercent: 11}, {UnixMillis: 3000, SRSCPUPercent: 12}, {UnixMillis: 4000, SRSCPUPercent: 13}}
	newer := []sample{{UnixMillis: 1300, SRSCPUPercent: 9}, {UnixMillis: 2700, SRSCPUPercent: 20}, {UnixMillis: 3100, SRSCPUPercent: 11}, {UnixMillis: 3900, SRSCPUPercent: 14}, {UnixMillis: 5000, SRSCPUPercent: 1}}
	a, b := pairBySecond(old, newer)
	wantA, wantB := []float64{10, 12, 13}, []float64{9, 11, 14}
	if len(a) != len(wantA) || len(b) != len(wantB) {
		t.Fatalf("pairs %v %v", a, b)
	}
	for i := range wantA {
		if a[i] != wantA[i] || b[i] != wantB[i] {
			t.Fatalf("pairs %v %v, want %v %v", a, b, wantA, wantB)
		}
	}
}
