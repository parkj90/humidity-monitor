# API Reference

## Authentication

No authentication is required. All devices are assumed to share a trusted local network.

## Path Parameters Reference

* **`monitor-id`** (string, required)
  * **Description:** The unique identifier of the monitoring device. Must contain 1-64 numbers, lowercase letters, or `-`.
  * **Validation:** Must match regex `^[a-z0-9-]{1,64}$`.
  * **Example:** `bedroom-monitor-1`

A path parameter that fails validation results in a `400` response.

## Error Response Body

Every error response (4xx) documented in this reference has a JSON body with a human-readable message:

| Field   | Type   | Description                                                    |
|---------|--------|----------------------------------------------------------------|
| `error` | string | Human-readable description of what was wrong with the request. |

## Objects

A *sample* is a single measurement taken by a monitor's sensor. A monitor does not send samples individually. It summarizes the samples taken over a short interval and sends that summary as a *reading*. Each reading reports the interval it spans but the samples taken within that interval are not assumed to be evenly spaced.

### Reading

| Field         | Type                | Description                                                                                |
|---------------|---------------------|--------------------------------------------------------------------------------------------|
| `measured_at` | number (int64)      | Unix timestamp in seconds of when the reading's interval closed.                           |
| `interval`    | number (int)        | The number of seconds spanned by the sampling cycles that contributed to the reading.      |
| `count`       | number (int)        | Number of successful samples taken in the reading.                                         |
| `humidity`    | object (Statistics) | Relative humidity statistics, in percent.                                                  |
| `temperature` | object (Statistics) | Temperature statistics, in Celsius.                                                        |

Interval is measured by the monitor from its own clock and can vary slightly from reading to reading. It starts when the monitor begins the sampling cycle of the reading's first successful sample and ends when the last successful sample is taken. It includes any failed samples in between but not failed attempts before the first or after the last success. Consecutive readings can therefore leave small gaps (for example, while a reading is being sent) and a longer gap means readings are missing.

### Statistics

| Field | Type           | Description                                     |
|-------|----------------|-------------------------------------------------|
| `min` | number (float) | Lowest value among the samples in the reading.  |
| `avg` | number (float) | Mean of the samples in the reading.             |
| `max` | number (float) | Highest value among the samples in the reading. |

### Validation

All fields are required, and a value of 0 is valid. A request that breaks a rule is rejected with a `400`, and only the first failure is reported.

| Field               | Rule                                                                            |
|---------------------|---------------------------------------------------------------------------------|
| `measured_at`       | Not before 2026-01-01T00:00:00Z. At most 2 minutes ahead of the server's clock. |
| `interval`          | From 1 to 60.                                                                   |
| `count`             | From 1 to 30.                                                                   |
| `humidity.*`        | From 0 to 100.                                                                  |
| `temperature.*`     | From -50 to 150.                                                                |
| `min`, `avg`, `max` | `min <= avg <= max` within each object.                                         |

**Example**

```json
{
  "measured_at": 1791887265,
  "interval": 10,
  "count": 5,
  "humidity":    { "min": 44.1, "avg": 45.2, "max": 46.0 },
  "temperature": { "min": 21.5, "avg": 21.7, "max": 21.9 }
}
```

## `GET` /monitors/{monitor-id}/readings

Returns the most recently submitted reading from a monitor device.

### Success response

**Code:** `200`

**Body:** a [Reading](#reading) object.

### Error response status codes

| Status code | Description             |
|-------------|-------------------------|
| `400`       | Invalid `monitor-id`.   |
| `404`       | `monitor-id` not found. |

## `POST` /monitors/{monitor-id}/readings

Submit a reading from a monitor device.

### Parameters

**Headers**

| Name           | Value              |
|----------------|--------------------|
| `Content-Type` | `application/json` |

**Body:** a [Reading](#reading) object.

### Response status codes

| Status code | Description                                                    |
|-------------|----------------------------------------------------------------|
| `201`       | Reading accepted.                                              |
| `400`       | Invalid request. See [Validation](#validation) and error body. |
| `413`       | Request body is too large. Limit is 1 MiB.                     |
| `415`       | `Content-Type` is missing or not `application/json`.           |

Devices should treat network errors as transient and retry. 4xx responses indicate a client error and should not be retried.
