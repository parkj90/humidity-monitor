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

## `GET` /monitors/{monitor-id}/readings

Returns the last submitted sensor reading from a monitor device.

### Success response

**Code:** `200`

**Body**

| Field         | Type           | Description                   |
|---------------|----------------|-------------------------------|
| `measured_at` | number (int64) | Unix timestamp in seconds.    |
| `humidity`    | number (float) | Relative humidity in percent. |
| `temperature` | number (float) | Temperature in Celsius.       |

### Error response status codes

| Status code | Description             |
|-------------|-------------------------|
| `400`       | Invalid `monitor-id`.   |
| `404`       | `monitor-id` not found. |

## `POST` /monitors/{monitor-id}/readings

Submit a sensor reading from a monitor device.

### Parameters

**Headers**

| Name           | Value              |
|----------------|--------------------|
| `Content-Type` | `application/json` |

**Body**

| Field         | Type           | Description                   |
|---------------|----------------|-------------------------------|
| `measured_at` | number (int64) | Unix timestamp in seconds.    |
| `humidity`    | number (float) | Relative humidity in percent. |
| `temperature` | number (float) | Temperature in Celsius.       |

### Response status codes

| Status code | Description                                                                         |
|-------------|-------------------------------------------------------------------------------------|
| `201`       | Reading accepted.                                                                   |
| `400`       | Malformed request (including extra fields) or invalid `monitor-id`. See error body. |
| `413`       | Request body is too large. Limit is 1 MiB.                                          |
| `415`       | `Content-Type` is missing or not `application/json`.                                |

Devices should treat network errors as transient and retry. 4xx responses indicate a client error and should not be retried.
