package app_test

import (
	"bytes"
	"encoding/json"
	"slices"
	"testing"

	"github.com/parkj90/humidity-monitor/server/app"
)

// The field names and types asserted here come from docs/api-contract.md, not from the
// Reading struct tags, so a renamed tag fails the test instead of passing silently.

func TestReadingKeysAndTypes(t *testing.T) {
	data, err := json.Marshal(app.Reading{
		MeasuredAt:  1791887265,
		Interval:    10,
		Count:       5,
		Humidity:    app.Statistics{Min: 44.1, Avg: 45.2, Max: 46.0},
		Temperature: app.Statistics{Min: 21.5, Avg: 21.7, Max: 21.9},
	})
	if err != nil {
		t.Fatalf("marshal reading: %v", err)
	}

	// UseNumber keeps numbers as json.Number instead of float64, so the integer-ness of
	// measured_at, interval and count survives the decode and can be checked below.
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	var got map[string]any
	if err := dec.Decode(&got); err != nil {
		t.Fatalf("decode reading: %v", err)
	}

	assertKeys(t, "reading", got, []string{"count", "humidity", "interval", "measured_at", "temperature"})

	for _, key := range []string{"measured_at", "interval", "count"} {
		n, ok := got[key].(json.Number)
		if !ok {
			t.Errorf("got %q of type %T, want a JSON number", key, got[key])
			continue
		}
		if _, err := n.Int64(); err != nil {
			t.Errorf("got %s %q, want an int64: %v", key, n, err)
		}
	}

	for _, key := range []string{"humidity", "temperature"} {
		stats, ok := got[key].(map[string]any)
		if !ok {
			t.Errorf("got %q of type %T, want a JSON object", key, got[key])
			continue
		}
		assertKeys(t, key, stats, []string{"avg", "max", "min"})
		for statKey, value := range stats {
			if _, ok := value.(json.Number); !ok {
				t.Errorf("got %s.%s of type %T, want a JSON number", key, statKey, value)
			}
		}
	}
}

func TestReadingFieldMapping(t *testing.T) {
	const contractExample = `{
		"measured_at": 1791887265,
		"interval": 10,
		"count": 5,
		"humidity":    {"min": 44.1, "avg": 45.2, "max": 46.0},
		"temperature": {"min": 21.5, "avg": 21.7, "max": 21.9}
	}`
	want := app.Reading{
		MeasuredAt:  1791887265,
		Interval:    10,
		Count:       5,
		Humidity:    app.Statistics{Min: 44.1, Avg: 45.2, Max: 46.0},
		Temperature: app.Statistics{Min: 21.5, Avg: 21.7, Max: 21.9},
	}

	var got app.Reading
	if err := json.Unmarshal([]byte(contractExample), &got); err != nil {
		t.Fatalf("unmarshal reading: %v", err)
	}

	if got != want {
		t.Errorf("got %+v, want %+v", got, want)
	}
}

func assertKeys(t *testing.T, name string, got map[string]any, want []string) {
	t.Helper()

	var gotKeys []string
	for key := range got {
		gotKeys = append(gotKeys, key)
	}
	slices.Sort(gotKeys)
	if !slices.Equal(gotKeys, want) {
		t.Errorf("got %s keys %v, want %v", name, gotKeys, want)
	}
}
