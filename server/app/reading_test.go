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
	data, err := json.Marshal(app.Reading{MeasuredAt: 1791887265, Humidity: 45.2, Temperature: 21.7})
	if err != nil {
		t.Fatalf("marshal reading: %v", err)
	}

	// UseNumber keeps numbers as json.Number instead of float64, so measured_at's
	// integer-ness survives the decode and can be checked below.
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	var got map[string]any
	if err := dec.Decode(&got); err != nil {
		t.Fatalf("decode reading: %v", err)
	}

	wantKeys := []string{"humidity", "measured_at", "temperature"}
	var gotKeys []string
	for key := range got {
		gotKeys = append(gotKeys, key)
	}
	slices.Sort(gotKeys)
	if !slices.Equal(gotKeys, wantKeys) {
		t.Errorf("got keys %v, want %v", gotKeys, wantKeys)
	}

	for _, key := range wantKeys {
		if _, ok := got[key].(json.Number); !ok {
			t.Errorf("got %q of type %T, want a JSON number", key, got[key])
		}
	}

	if n, ok := got["measured_at"].(json.Number); ok {
		if _, err := n.Int64(); err != nil {
			t.Errorf("got measured_at %q, want an int64: %v", n, err)
		}
	}
}

func TestReadingFieldMapping(t *testing.T) {
	const contractExample = `{"measured_at":1791887265,"humidity":45.2,"temperature":21.7}`
	want := app.Reading{MeasuredAt: 1791887265, Humidity: 45.2, Temperature: 21.7}

	var got app.Reading
	if err := json.Unmarshal([]byte(contractExample), &got); err != nil {
		t.Fatalf("unmarshal reading: %v", err)
	}

	if got != want {
		t.Errorf("got %+v, want %+v", got, want)
	}
}
