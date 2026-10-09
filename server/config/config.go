package config

import (
	"flag"
	"fmt"
	"io"
)

type Config struct {
	Host string
	Port string
}

func Parse(stderr io.Writer, args []string) (Config, error) {
	var cfg Config
	fs := flag.NewFlagSet(args[0], flag.ContinueOnError)
	fs.StringVar(&cfg.Host, "host", "0.0.0.0", "host to listen on")
	fs.StringVar(&cfg.Port, "port", "8080", "port to listen on")

	// Suppress flag's built-in error output; Usage temporarily re-enables stderr
	fs.SetOutput(io.Discard)
	fs.Usage = func() {
		fmt.Fprintf(stderr, "Usage: %s [flags]\n\nFlags:\n", fs.Name())
		fs.SetOutput(stderr)
		fs.PrintDefaults()
		fs.SetOutput(io.Discard)
	}

	if err := fs.Parse(args[1:]); err != nil {
		return Config{}, fmt.Errorf("parse args: %w", err)
	}
	return cfg, nil
}
