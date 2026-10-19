package app

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"mime"
	"net/http"
	"regexp"
)

type apiErrorDetails struct {
	status   int
	err      error
	response string
}

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

		// TODO validate the request body more strictly
		reading, err := decode[Reading](r)
		if err != nil {
			encodeError(logger, w, r, apiErrorDetails{
				status:   http.StatusBadRequest,
				err:      err,
				response: "request body must be valid JSON",
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
	if err := json.NewDecoder(r.Body).Decode(&v); err != nil {
		return v, fmt.Errorf("decode json: %w", err)
	}
	return v, nil
}
