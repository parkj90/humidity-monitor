package app

import (
	"log/slog"
	"net/http"
	"time"
)

func NewServer(
	logger *slog.Logger,
	now func() time.Time,
) http.Handler {
	mux := http.NewServeMux()
	addRoutes(
		mux,
		logger,
		now,
	)
	var handler http.Handler = mux
	return handler
}
