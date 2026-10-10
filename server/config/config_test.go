package config_test

import (
	"bytes"
	"errors"
	"flag"
	"strings"
	"testing"

	"github.com/parkj90/humidity-monitor/server/config"
)

func TestParse(t *testing.T) {
	tests := []struct {
		name   string
		args   []string
		want   config.Config
		err    error
		errStr string
	}{
		{
			name: "defaults",
			args: []string{"server"},
			want: config.Config{Host: "0.0.0.0", Port: "8080"},
		},
		{
			name: "overrides",
			args: []string{"server", "-host", "127.0.0.1", "-port", "7070"},
			want: config.Config{Host: "127.0.0.1", Port: "7070"},
		},
		{
			name: "help flag",
			args: []string{"server", "-h"},
			want: config.Config{},
			err:  flag.ErrHelp,
		},
		{
			name:   "unknown flag",
			args:   []string{"server", "-foo"},
			want:   config.Config{},
			errStr: "flag provided but not defined: -foo",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			var buf bytes.Buffer
			got, err := config.Parse(&buf, test.args)

			if got != test.want {
				t.Errorf("got config %+v, want %+v", got, test.want)
			}

			switch {
			case test.err != nil:
				if !errors.Is(err, test.err) {
					t.Errorf("got err %v, want %v", err, test.err)
				}
			case test.errStr != "":
				if err == nil || !strings.Contains(err.Error(), test.errStr) {
					t.Errorf("got err %v, want error containing %q", err, test.errStr)
				}
			case err != nil:
				t.Errorf("got err %v, want nil", err)
			}
		})
	}
}
