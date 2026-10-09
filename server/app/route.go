package app

import (
	"log/slog"
	"net/http"
)

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
	})
}

func handleCreateReading(logger *slog.Logger) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		logger.Debug("handling reading create")
	})
}
