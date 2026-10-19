package app_test

import (
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/parkj90/humidity-monitor/server/app"
)

const validBody = `{"measured_at":1791887265,"humidity":45.2,"temperature":21.7}`

func newTestServer() http.Handler {
	return app.NewServer(slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func TestGetReading(t *testing.T) {
	tests := []struct {
		name      string
		monitorID string
		want      int
	}{
		{name: "valid id", monitorID: "test-monitor-1", want: http.StatusOK},
		{name: "single character", monitorID: "a", want: http.StatusOK},
		{name: "max length", monitorID: strings.Repeat("a", 64), want: http.StatusOK},
		{name: "too long", monitorID: strings.Repeat("a", 65), want: http.StatusBadRequest},
		{name: "uppercase", monitorID: "Test", want: http.StatusBadRequest},
		{name: "underscore", monitorID: "test_1", want: http.StatusBadRequest},
		{name: "dot", monitorID: "test.1", want: http.StatusBadRequest},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			req := httptest.NewRequest(http.MethodGet, "/monitors/"+test.monitorID+"/readings", nil)
			rec := httptest.NewRecorder()

			newTestServer().ServeHTTP(rec, req)
			res := rec.Result()
			defer res.Body.Close()

			if res.StatusCode != test.want {
				t.Fatalf("got status %d, want %d", res.StatusCode, test.want)
			}
			if got := res.Header.Get("Content-Type"); got != "application/json" {
				t.Errorf("got Content-Type %q, want %q", got, "application/json")
			}
			if test.want != http.StatusOK {
				assertJSONError(t, res, "invalid monitor-id")
				return
			}

			var reading app.Reading
			if err := json.NewDecoder(res.Body).Decode(&reading); err != nil {
				t.Fatalf("decode reading: %v", err)
			}
		})
	}
}

func TestCreateReading(t *testing.T) {
	tests := []struct {
		name        string
		monitorID   string
		contentType string
		body        string
		want        int
		wantError   string // exact error message; empty means only check that one is present
	}{
		{
			name:        "valid request",
			monitorID:   "test-monitor-1",
			contentType: "application/json",
			body:        validBody,
			want:        http.StatusCreated,
		},
		{
			name:        "content type with charset",
			monitorID:   "test-monitor-1",
			contentType: "application/json; charset=utf-8",
			body:        validBody,
			want:        http.StatusCreated,
		},
		{
			name:        "content type is case-insensitive",
			monitorID:   "test-monitor-1",
			contentType: "APPLICATION/JSON",
			body:        validBody,
			want:        http.StatusCreated,
		},
		{
			name:      "missing content type",
			monitorID: "test-monitor-1",
			body:      validBody,
			want:      http.StatusUnsupportedMediaType,
		},
		{
			name:        "wrong content type",
			monitorID:   "test-monitor-1",
			contentType: "text/plain",
			body:        validBody,
			want:        http.StatusUnsupportedMediaType,
		},
		{
			name:        "malformed content type",
			monitorID:   "test-monitor-1",
			contentType: "application/json; charset",
			body:        validBody,
			want:        http.StatusUnsupportedMediaType,
		},
		{
			name:        "invalid json",
			monitorID:   "test-monitor-1",
			contentType: "application/json",
			body:        `{not json`,
			want:        http.StatusBadRequest,
			wantError:   "request body must be valid JSON",
		},
		{
			name:        "empty body",
			monitorID:   "test-monitor-1",
			contentType: "application/json",
			body:        "",
			want:        http.StatusBadRequest,
			wantError:   "request body must be valid JSON",
		},
		{
			name:        "wrong field type",
			monitorID:   "test-monitor-1",
			contentType: "application/json",
			body:        `{"humidity":"high"}`,
			want:        http.StatusBadRequest,
			wantError:   "request body must be valid JSON",
		},
		{
			name:        "invalid monitor id",
			monitorID:   "Test",
			contentType: "application/json",
			body:        validBody,
			want:        http.StatusBadRequest,
			wantError:   "invalid monitor-id",
		},
		{
			name:      "invalid monitor id is reported before content type",
			monitorID: strings.Repeat("a", 65),
			body:      validBody,
			want:      http.StatusBadRequest,
			wantError: "invalid monitor-id",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			req := httptest.NewRequest(http.MethodPost, "/monitors/"+test.monitorID+"/readings", strings.NewReader(test.body))
			if test.contentType != "" {
				req.Header.Set("Content-Type", test.contentType)
			}
			rec := httptest.NewRecorder()

			newTestServer().ServeHTTP(rec, req)
			res := rec.Result()
			defer res.Body.Close()

			if res.StatusCode != test.want {
				t.Fatalf("got status %d, want %d", res.StatusCode, test.want)
			}
			if test.want >= http.StatusBadRequest {
				if got := res.Header.Get("Content-Type"); got != "application/json" {
					t.Errorf("got Content-Type %q, want %q", got, "application/json")
				}
				assertJSONError(t, res, test.wantError)
			}
		})
	}
}

// assertJSONError checks that the response body has a non-empty "error" field. If wantError
// is not empty, the field must equal it exactly.
func assertJSONError(t *testing.T, res *http.Response, wantError string) {
	t.Helper()

	var body struct {
		Error string `json:"error"`
	}
	if err := json.NewDecoder(res.Body).Decode(&body); err != nil {
		t.Fatalf("decode error body: %v", err)
	}
	switch {
	case body.Error == "":
		t.Error("got empty error message, want non-empty")
	case wantError != "" && body.Error != wantError:
		t.Errorf("got error message %q, want %q", body.Error, wantError)
	}
}
