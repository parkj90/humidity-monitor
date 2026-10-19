package app

type Reading struct {
	MeasuredAt  int64   `json:"measured_at"`
	Humidity    float64 `json:"humidity"`
	Temperature float64 `json:"temperature"`
}
