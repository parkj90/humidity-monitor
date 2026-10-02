# API Reference

## Authentication

No authentication is required. All devices are assumed to share a trusted local network.

## Path Parameters Reference

* **`monitor-id`** (string, required)
  * **Description:** The unique identifier of the monitoring device.
  * **Validation:** Must match regex `^[a-z0-9-]+$`.
  * **Example:** `bedroom-monitor-1`

A path parameter that fails validation results in a `400` response.

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

| Status code | Description                        |
|-------------|------------------------------------|
| `201`       | Reading accepted.                  |
| `400`       | Malformed request. See error body. |

Devices should treat network errors as transient and retry. 4xx responses indicate a client error and should not be retried.
