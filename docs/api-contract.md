# API Reference

## Authentication

No authentication is required. All devices are assumed to share a trusted local network.

## `POST` /readings

Submit a sensor reading from a monitor device.

### Parameters

**Headers**

| Name           | Value              |
|----------------|--------------------|
| `Content-Type` | `application/json` |

**Body**

| Field         | Type           | Description                                                |
|---------------|----------------|------------------------------------------------------------|
| `device_id`   | string         | MAC address of the device. Example: `"aa:bb:cc:dd:ee:ff"`. |
| `humidity`    | number (float) | Relative humidity in percent.                              |
| `temperature` | number (float) | Temperature in Celsius.                                    |

> **TODO:** Add a `measured_at` timestamp field.

### Response status codes

| Status code | Description                        |
|-------------|------------------------------------|
| `201`       | Reading accepted.                  |
| `400`       | Malformed request. See error body. |

Devices should treat network errors as transient and retry. 4xx responses indicate a client error and should not be retried.
