package app

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"
)

const validJSON = `{"measured_at":1793448000,"interval":10,"count":5,"humidity":{"min":44.1,"avg":45.2,"max":46.0},"temperature":{"min":21.5,"avg":21.7,"max":21.9}}`

// matches measured_at in validJSON
var testNow = time.Unix(1793448000, 0)

func TestValidateMapping(t *testing.T) {
	got, err := decodeUnvalidated(t, validJSON).validate(testNow)
	if err != nil {
		t.Fatalf("validate: %v", err)
	}
	want := Reading{
		MeasuredAt:  1793448000,
		Interval:    10,
		Count:       5,
		Humidity:    Statistics{Min: 44.1, Avg: 45.2, Max: 46.0},
		Temperature: Statistics{Min: 21.5, Avg: 21.7, Max: 21.9},
	}
	if got != want {
		t.Errorf("got %+v, want %+v", got, want)
	}
}

func TestValidate(t *testing.T) {
	const (
		measuredAt  = `"measured_at":1793448000`
		interval    = `"interval":10`
		count       = `"count":5`
		humidity    = `"humidity":{"min":44.1,"avg":45.2,"max":46.0}`
		temperature = `"temperature":{"min":21.5,"avg":21.7,"max":21.9}`
		floor       = 1767225600 // 2026-01-01T00:00:00Z
	)
	ceiling := testNow.Add(2 * time.Minute).Unix()

	// returns validJSON with one fragment replaced
	jsonWith := func(old, replacement string) string {
		if !strings.Contains(validJSON, old) {
			t.Fatalf("validJSON does not contain %q", old)
		}
		return strings.Replace(validJSON, old, replacement, 1)
	}

	tests := []struct {
		name      string
		json      string
		wantError string // empty means the reading must be valid
	}{
		{name: "no fields", json: `{}`, wantError: "measured_at is required"},
		{name: "null fields", json: `{"measured_at":null,"interval":null,"count":null,"humidity":null,"temperature":null}`, wantError: "measured_at is required"},

		{name: "measured_at missing", json: jsonWith(measuredAt+",", ""), wantError: "measured_at is required"},
		{name: "measured_at at floor", json: jsonWith(measuredAt, fmt.Sprintf(`"measured_at":%d`, floor))},
		{name: "measured_at before floor", json: jsonWith(measuredAt, fmt.Sprintf(`"measured_at":%d`, floor-1)), wantError: "measured_at must not be before 2026-01-01T00:00:00Z"},
		{name: "measured_at zero", json: jsonWith(measuredAt, `"measured_at":0`), wantError: "measured_at must not be before 2026-01-01T00:00:00Z"},
		{name: "measured_at at ceiling", json: jsonWith(measuredAt, fmt.Sprintf(`"measured_at":%d`, ceiling))},
		{name: "measured_at past ceiling", json: jsonWith(measuredAt, fmt.Sprintf(`"measured_at":%d`, ceiling+1)), wantError: "measured_at must be at most 2m0s ahead of the server's clock"},

		{name: "interval missing", json: jsonWith(interval+",", ""), wantError: "interval is required"},
		{name: "interval minimum", json: jsonWith(interval, `"interval":1`)},
		{name: "interval maximum", json: jsonWith(interval, `"interval":60`)},
		{name: "interval zero", json: jsonWith(interval, `"interval":0`), wantError: "interval must be between 1 and 60"},
		{name: "interval too large", json: jsonWith(interval, `"interval":61`), wantError: "interval must be between 1 and 60"},

		{name: "count missing", json: jsonWith(count+",", ""), wantError: "count is required"},
		{name: "count minimum", json: jsonWith(count, `"count":1`)},
		{name: "count maximum", json: jsonWith(count, `"count":30`)},
		{name: "count zero", json: jsonWith(count, `"count":0`), wantError: "count must be between 1 and 30"},
		{name: "count too large", json: jsonWith(count, `"count":31`), wantError: "count must be between 1 and 30"},

		{name: "humidity missing", json: jsonWith(","+humidity, ""), wantError: "humidity.min is required"},
		{name: "humidity avg missing", json: jsonWith(`"avg":45.2,`, ""), wantError: "humidity.avg is required"},
		{name: "humidity max null", json: jsonWith(`"max":46.0`, `"max":null`), wantError: "humidity.max is required"},
		{name: "humidity lower bound", json: jsonWith(humidity, `"humidity":{"min":0,"avg":0,"max":0}`)},
		{name: "humidity upper bound", json: jsonWith(humidity, `"humidity":{"min":100,"avg":100,"max":100}`)},
		{name: "humidity below range", json: jsonWith(`"min":44.1`, `"min":-0.1`), wantError: "humidity.min must be between 0 and 100"},
		{name: "humidity above range", json: jsonWith(`"max":46.0`, `"max":100.1`), wantError: "humidity.max must be between 0 and 100"},
		{name: "humidity min above avg", json: jsonWith(`"min":44.1`, `"min":45.3`), wantError: "humidity must satisfy min <= avg <= max"},
		{name: "humidity avg above max", json: jsonWith(`"avg":45.2`, `"avg":46.1`), wantError: "humidity must satisfy min <= avg <= max"},
		{name: "humidity equal min avg max", json: jsonWith(humidity, `"humidity":{"min":45,"avg":45,"max":45}`)},

		{name: "temperature missing", json: jsonWith(","+temperature, ""), wantError: "temperature.min is required"},
		{name: "temperature lower bound", json: jsonWith(temperature, `"temperature":{"min":-50,"avg":-50,"max":-50}`)},
		{name: "temperature upper bound", json: jsonWith(temperature, `"temperature":{"min":150,"avg":150,"max":150}`)},
		{name: "temperature below range", json: jsonWith(`"min":21.5`, `"min":-50.1`), wantError: "temperature.min must be between -50 and 150"},
		{name: "temperature above range", json: jsonWith(`"max":21.9`, `"max":150.1`), wantError: "temperature.max must be between -50 and 150"},
		{name: "temperature min above avg", json: jsonWith(`"min":21.5`, `"min":21.8`), wantError: "temperature must satisfy min <= avg <= max"},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			got, err := decodeUnvalidated(t, test.json).validate(testNow)

			if test.wantError == "" {
				if err != nil {
					t.Fatalf("got error %q, want a valid reading", err)
				}
				return
			}
			if err == nil {
				t.Fatalf("got no error, want %q", test.wantError)
			}
			if err.Error() != test.wantError {
				t.Errorf("got error %q, want %q", err, test.wantError)
			}
			if got != (Reading{}) {
				t.Errorf("got reading %+v alongside an error, want the zero Reading", got)
			}
		})
	}
}

func decodeUnvalidated(t *testing.T, data string) unvalidatedReading {
	t.Helper()

	var u unvalidatedReading
	if err := json.Unmarshal([]byte(data), &u); err != nil {
		t.Fatalf("unmarshal reading: %v", err)
	}
	return u
}
