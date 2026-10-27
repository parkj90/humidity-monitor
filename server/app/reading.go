package app

type Statistics struct {
	Min float64 `json:"min"`
	Avg float64 `json:"avg"`
	Max float64 `json:"max"`
}

type Reading struct {
	MeasuredAt  int64      `json:"measured_at"`
	Interval    int        `json:"interval"`
	Count       int        `json:"count"`
	Humidity    Statistics `json:"humidity"`
	Temperature Statistics `json:"temperature"`
}
