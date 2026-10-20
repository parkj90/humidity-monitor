package app

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"mime"
	"net/http"
	"regexp"
	"strings"
)

type apiErrorDetails struct {
	status   int
	err      error
	response string
}

// keep in sync with the 413 row in docs/api-contract.md
const maxRequestBodyBytes = 1 << 20 // 1 MiB

var validMonitorID = regexp.MustCompile("^[a-z0-9-]{1,64}$")

func addRoutes(
	mux *http.ServeMux,
	logger *slog.Logger,
) {
	mux.Handle("GET /monitors/{monitorID}/readings", handleGetReading(logger))
	mux.Handle("POST /monitors/{monitorID}/readings", handleCreateReading(logger))
}

func handleGetReading(logger *slog.Logger) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		logger.Debug("handling reading get")

		monitorID, ok := getValidatedMonitorID(logger, w, r)
		if !ok {
			return
		}
		logger.Debug("validated monitorID", "monitorID", monitorID)

		// TODO replace with stored Reading
		if err := encode(w, http.StatusOK, Reading{MeasuredAt: 1791887265, Humidity: 45.2, Temperature: 21.7}); err != nil {
			logger.Error("failed to encode Reading response", "err", err)
		}
	})
}

func handleCreateReading(logger *slog.Logger) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		logger.Debug("handling reading create")

		monitorID, ok := getValidatedMonitorID(logger, w, r)
		if !ok {
			return
		}
		logger.Debug("validated monitorID", "monitorID", monitorID)

		ct := r.Header.Get("Content-Type")
		mediatype, _, err := mime.ParseMediaType(ct)
		if err != nil || mediatype != "application/json" {
			if err == nil {
				err = fmt.Errorf("invalid mediatype: %q", mediatype)
			}
			encodeError(logger, w, r, apiErrorDetails{
				status:   http.StatusUnsupportedMediaType,
				err:      err,
				response: "Content-Type header must be application/json",
			})
			return
		}

		r.Body = http.MaxBytesReader(w, r.Body, maxRequestBodyBytes)

		reading, err := decode[Reading](r)
		if err != nil {
			status := http.StatusBadRequest
			var response string
			var maxBytesError *http.MaxBytesError
			var syntaxError *json.SyntaxError
			var unmarshalTypeError *json.UnmarshalTypeError
			switch {
			case errors.As(err, &maxBytesError):
				status = http.StatusRequestEntityTooLarge
				response = fmt.Sprintf("maximum bytes (%d) exceeded in request body", maxBytesError.Limit)
			case errors.As(err, &syntaxError):
				response = fmt.Sprintf("syntax error detected at position: %d in request body", syntaxError.Offset)
			case errors.As(err, &unmarshalTypeError):
				response = fmt.Sprintf("invalid value for the %s field detected in request body", unmarshalTypeError.Field)
			case strings.Contains(err.Error(), "unknown field"):
				response = "unknown field detected in request body JSON"
			case errors.Is(err, io.EOF):
				response = "request body is empty"
			default:
				response = "request body must be valid JSON"
			}
			encodeError(logger, w, r, apiErrorDetails{
				status:   status,
				err:      err,
				response: response,
			})
			return
		}
		logger.Debug("received reading", "reading", reading)

		w.WriteHeader(http.StatusCreated)
	})
}

func getValidatedMonitorID(logger *slog.Logger, w http.ResponseWriter, r *http.Request) (string, bool) {
	id := r.PathValue("monitorID")
	if !validMonitorID.MatchString(id) {
		encodeError(logger, w, r, apiErrorDetails{
			status:   http.StatusBadRequest,
			err:      fmt.Errorf("invalid monitorID: %q", id),
			response: "invalid monitor-id",
		})
		return "", false
	}
	return id, true
}

func encodeError(logger *slog.Logger, w http.ResponseWriter, r *http.Request, details apiErrorDetails) {
	args := []any{"status", details.status, "method", r.Method, "path", r.URL.Path, "response", details.response}
	if details.err != nil {
		args = append(args, "err", details.err)
	}
	msg, level := "client error", slog.LevelDebug
	if details.status >= http.StatusInternalServerError {
		msg, level = "server error", slog.LevelError
	}
	logger.Log(r.Context(), level, msg, args...)

	if err := encode(w, details.status, struct {
		Error string `json:"error"`
	}{details.response}); err != nil {
		logger.Error("failed to encode error response", "err", err)
	}
}

func encode[T any](w http.ResponseWriter, status int, v T) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(v); err != nil {
		return fmt.Errorf("encode json: %w", err)
	}
	return nil
}

func decode[T any](r *http.Request) (T, error) {
	var v T
	dec := json.NewDecoder(r.Body)
	dec.DisallowUnknownFields()
	if err := dec.Decode(&v); err != nil {
		return v, fmt.Errorf("decode json: %w", err)
	}
	return v, nil
}
