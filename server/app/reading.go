package app

import (
	"errors"
	"fmt"
	"time"
)

// Keep in sync with specifications in docs/api-contract.md
const (
	minMeasuredAt      = 1767225600      // 2026-01-01T00:00:00Z. Ref Validation: measured_at
	maxMeasuredAtDrift = 2 * time.Minute // Ref Validation: measured_at

	minInterval, maxInterval       = 1, 60        // Ref Validation: interval
	minCount, maxCount             = 1, 30        // Ref Validation: count
	minHumidity, maxHumidity       = 0.0, 100.0   // Ref Validation: humidity
	minTemperature, maxTemperature = -50.0, 150.0 // Ref Validation: temperature
)

type Statistics struct {
	Min float64 `json:"min"`
	Avg float64 `json:"avg"`
	Max float64 `json:"max"`
}

type Reading struct {
	MeasuredAt  int64      `json:"measured_at"`
	Interval    int        `json:"interval"`
	Count       int        `json:"count"`
	Humidity    Statistics `json:"humidity"`
	Temperature Statistics `json:"temperature"`
}

type unvalidatedStatistics struct {
	Min *float64 `json:"min"`
	Avg *float64 `json:"avg"`
	Max *float64 `json:"max"`
}

type unvalidatedReading struct {
	MeasuredAt  *int64                `json:"measured_at"`
	Interval    *int                  `json:"interval"`
	Count       *int                  `json:"count"`
	Humidity    unvalidatedStatistics `json:"humidity"`
	Temperature unvalidatedStatistics `json:"temperature"`
}

// Every err returned by validate is meant to be used as the client response
func (u unvalidatedReading) validate(now time.Time) (Reading, error) {
	measuredAt, err := validateMeasuredAt(u.MeasuredAt, now)
	if err != nil {
		return Reading{}, err
	}
	interval, err := validateRange("interval", u.Interval, minInterval, maxInterval)
	if err != nil {
		return Reading{}, err
	}
	count, err := validateRange("count", u.Count, minCount, maxCount)
	if err != nil {
		return Reading{}, err
	}
	humidity, err := u.Humidity.validate("humidity", minHumidity, maxHumidity)
	if err != nil {
		return Reading{}, err
	}
	temperature, err := u.Temperature.validate("temperature", minTemperature, maxTemperature)
	if err != nil {
		return Reading{}, err
	}
	return Reading{
		MeasuredAt:  measuredAt,
		Interval:    interval,
		Count:       count,
		Humidity:    humidity,
		Temperature: temperature,
	}, nil
}

func (u unvalidatedStatistics) validate(name string, lo, hi float64) (Statistics, error) {
	lowest, err := validateRange(name+".min", u.Min, lo, hi)
	if err != nil {
		return Statistics{}, err
	}
	average, err := validateRange(name+".avg", u.Avg, lo, hi)
	if err != nil {
		return Statistics{}, err
	}
	highest, err := validateRange(name+".max", u.Max, lo, hi)
	if err != nil {
		return Statistics{}, err
	}
	if lowest > average || average > highest {
		return Statistics{}, fmt.Errorf("%s must satisfy min <= avg <= max", name)
	}
	return Statistics{Min: lowest, Avg: average, Max: highest}, nil
}

func validateMeasuredAt(v *int64, now time.Time) (int64, error) {
	if v == nil {
		return 0, errors.New("measured_at is required")
	}
	if *v < minMeasuredAt {
		return 0, fmt.Errorf("measured_at must not be before %s", time.Unix(minMeasuredAt, 0).UTC().Format(time.RFC3339))
	}
	if *v > now.Add(maxMeasuredAtDrift).Unix() {
		return 0, fmt.Errorf("measured_at must be at most %s ahead of the server's clock", maxMeasuredAtDrift)
	}
	return *v, nil
}

func validateRange[T int | float64](name string, v *T, lo, hi T) (T, error) {
	var zero T
	if v == nil {
		return zero, fmt.Errorf("%s is required", name)
	}
	if *v < lo || *v > hi {
		return zero, fmt.Errorf("%s must be between %v and %v", name, lo, hi)
	}
	return *v, nil
}
