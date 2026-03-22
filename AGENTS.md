# AGENTS.md

## Project Scope
`info-getter` is a **C++ backend REST service** for collecting weather sensor readings and querying aggregated metrics from persisted data.

## Current Phase: Implementation
1. Implementation is now active.
1. Build the service contract-first from the API definitions in this document.
1. Keep scope focused on required functionality; avoid unnecessary complexity.

## Non-Negotiable Constraints
1. Language: **C++**.
1. Interface: **REST API** with JSON request/response bodies.
1. Persistence: **database-backed** (durable storage; no in-memory-only solution).
1. Preferred database: **MySQL** (primary target unless a simpler operational alternative is approved).
1. UI: **none**.

## Engineering Process And Test Strategy
1. Follow **Red/Green TDD**: write a failing test first, implement the minimum change to pass, then refactor.
1. Prioritize **unit tests** for validation, date-window logic, aggregation rules, and request/response mapping.
1. Add **integration tests** after unit coverage is in place for API + DB behavior.
1. Complete test coverage is not required; coverage must be sufficient for core business logic and critical error paths.
1. Every API acceptance criterion should map to at least one automated test.

## API Design (Planned v1)

### 1) Ingest Sensor Reading
- **Method/Path**: `POST /api/v1/readings`
- **Purpose**: Receive new weather metrics from a sensor.
- **Request Body**:

```json
{
  "sensorId": "sensor-1",
  "observedAt": "2026-03-21T10:00:00Z",
  "metrics": {
    "temperature": 21.4,
    "humidity": 48.2,
    "windSpeed": 5.8
  }
}
```

- **Validation Rules**:
1. `sensorId` is required.
1. `observedAt` is required and must be ISO-8601 UTC timestamp.
1. `metrics` is required and must include at least one numeric metric.
1. Metric values must be finite numbers.

- **Success Response**: `201 Created`

```json
{
  "id": "reading-uuid",
  "sensorId": "sensor-1",
  "observedAt": "2026-03-21T10:00:00Z",
  "stored": true
}
```

### 2) Query Aggregated Metrics
- **Method/Path**: `POST /api/v1/queries/aggregate`
- **Purpose**: Query one or more sensors (or all sensors) and return aggregation results (`min`, `max`, `sum`, `avg`) for one or more metrics.
- **Request Body**:

```json
{
  "sensorIds": ["sensor-1"],
  "metrics": ["temperature", "humidity"],
  "aggregations": ["avg"],
  "dateRange": {
    "from": "2026-03-14T00:00:00Z",
    "to": "2026-03-21T00:00:00Z"
  }
}
```

- **Behavior Rules**:
1. `sensorIds` omitted or empty means all sensors.
1. `metrics` is required and must contain at least one metric name.
1. `aggregations` is required; allowed values are exactly `min`, `max`, `sum`, `avg`.
1. If `dateRange` is present, both `from` and `to` are required.
1. Explicit `dateRange` must be between **1 day and 31 days** (inclusive).
1. If `dateRange` is not provided, default window is the **latest 24 hours ending at the latest available reading timestamp** in scope.

- **Success Response**: `200 OK`

```json
{
  "window": {
    "from": "2026-03-14T00:00:00Z",
    "to": "2026-03-21T00:00:00Z",
    "mode": "explicit"
  },
  "results": [
    {
      "sensorId": "sensor-1",
      "metric": "temperature",
      "aggregation": "avg",
      "value": 20.12
    },
    {
      "sensorId": "sensor-1",
      "metric": "humidity",
      "aggregation": "avg",
      "value": 49.88
    }
  ]
}
```

- **Example Requirement Mapping**:
1. Requesting “average temperature and humidity for sensor 1 in the last week” maps to:
   `sensorIds=["sensor-1"]`, `metrics=["temperature","humidity"]`, `aggregations=["avg"]`, `dateRange=last 7 days`.

## Error Contract (Both Endpoints)
1. Use structured JSON errors.
1. Validation failures return `400 Bad Request`.
1. Unknown sensors/metrics in filters return `400 Bad Request`.
1. Server/data-layer failures return `5xx`.
1. Exception handling must prevent process crashes for bad input or expected runtime failures.
1. Internal exception details (stack traces, driver internals) must not be exposed to API clients.

Example:

```json
{
  "error": {
    "code": "INVALID_DATE_RANGE",
    "message": "dateRange must be between 1 and 31 days"
  }
}
```

## Persistence Expectations (Planning)
1. Data must survive process restarts.
1. Readings must be queryable by sensor and time range.
1. Aggregations (`min`, `max`, `sum`, `avg`) must be executed against persisted data.

## Database Direction (Planning)
1. Primary DB choice is **MySQL**.
1. If MySQL proves heavy for local developer workflow, evaluate an easier alternative only if it still supports required aggregations and date-range performance.
1. Any DB change from MySQL must be recorded with rationale before implementation.

## Database Schema (Planned v1, Minimal)
1. Keep schema simple and functional; no optional columns in v1.
1. Use two tables:

### `sensors`
1. `sensor_id` VARCHAR(...) PRIMARY KEY.

### `readings`
1. `sensor_id` VARCHAR(...) NOT NULL.
1. `observed_at` DATETIME(6) NOT NULL.
1. `temperature` DOUBLE NOT NULL.
1. `humidity` DOUBLE NOT NULL.
1. `wind_speed` DOUBLE NOT NULL.
1. PRIMARY KEY (`sensor_id`, `observed_at`).
1. FOREIGN KEY (`sensor_id`) REFERENCES `sensors`(`sensor_id`).
1. INDEX on `observed_at` for range filtering.

## Schema Rationale
1. `sensor_id` alone cannot be primary key for weather history, because each sensor needs many readings over time.
1. Composite key (`sensor_id`, `observed_at`) keeps uniqueness simple without introducing extra surrogate IDs in v1.
1. The model supports required per-sensor and all-sensor date-range aggregations (`min`, `max`, `sum`, `avg`).

## Acceptance Criteria (Implementation-Ready)
1. `POST /api/v1/readings` with valid payload stores a record and returns `201`.
1. Stored readings remain available after service restart (durable DB persistence).
1. Aggregate query supports a single sensor, multiple sensors, and all sensors (when omitted).
1. Aggregate query supports `min`, `max`, `sum`, `avg` for requested metrics.
1. Explicit date range queries succeed only when range is between 1 and 31 days inclusive.
1. Date ranges outside the allowed bounds return `400` with clear error code/message.
1. If no date range is supplied, query defaults to latest 24h window ending at latest available data in scope.
1. Example scenario passes: average `temperature` and `humidity` for `sensor-1` over last 7 days returns correct aggregated values.
1. Invalid payloads (missing fields, invalid timestamp, non-numeric metric values, unsupported aggregation) return `400`.
1. API responses are JSON and include enough metadata to identify query window and aggregation context.
1. Known error conditions are handled via structured responses without leaking internal exception details.
1. Unit tests are written first (Red/Green) for core business logic; integration tests validate API-to-DB behavior for key scenarios.

## Next-Phase Implementation Guidance
1. Choose C++ web framework and DB layer that support production-ready REST + SQL access, with MySQL as default.
1. Finalize database schema for sensor readings and metric storage.
1. Implement contract-first from this document under Red/Green TDD.
1. Start with unit tests, then add integration tests for API and persistence flows.

## Planning Phase Exit Criteria
1. APIs in this document are the approved v1 contract baseline for implementation.
1. Acceptance criteria in this document are testable and mapped to TDD-driven development.
