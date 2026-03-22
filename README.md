# info-getter

C++ REST service for ingesting and querying aggregated weather sensor metrics.

## Docker

```bash
docker compose up -d
```

The service starts on `http://localhost:8080` with data persisted in a named volume (`info_getter_data`).

To rebuild after code changes:

```bash
docker compose up -d --build
```

To stop:

```bash
docker compose down
```

## Local Build

### Prerequisites

- C++23 compiler (GCC 15+)
- CMake 3.20+

### Build

```bash
cd /home/dashp/info-getter
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

```bash
./build/info_getter_server
```

**Defaults:** `0.0.0.0:8080`, database: `info_getter.db`

**Environment variables:**
```bash
INFO_GETTER_HOST=127.0.0.1 INFO_GETTER_PORT=9000 INFO_GETTER_DB_PATH=/path/to/db ./build/info_getter_server
```

### Testing

```bash
./build/unit_tests
./build/integration_tests
cd build && ctest
```

## API

### POST /api/v1/readings — Ingest reading

```bash
curl -X POST http://localhost:8080/api/v1/readings \
  -H "Content-Type: application/json" \
  -d '{
    "sensorId": "sensor-1",
    "observedAt": "2026-03-21T10:00:00Z",
    "metrics": {"temperature": 21.4, "humidity": 48.2, "windSpeed": 5.8}
  }'
```

**Response:** `201 Created`
```json
{"id": "uuid", "sensorId": "sensor-1", "observedAt": "2026-03-21T10:00:00Z", "stored": true}
```

### POST /api/v1/queries/aggregate — Query metrics

```bash
curl -X POST http://localhost:8080/api/v1/queries/aggregate \
  -H "Content-Type: application/json" \
  -d '{
    "sensorIds": ["sensor-1"],
    "metrics": ["temperature", "humidity"],
    "aggregations": ["avg", "min", "max"],
    "dateRange": {"from": "2026-03-14T00:00:00Z", "to": "2026-03-21T00:00:00Z"}
  }'
```

**Response:** `200 OK`
```json
{
  "window": {"from": "2026-03-14T00:00:00Z", "to": "2026-03-21T00:00:00Z", "mode": "explicit"},
  "results": [
    {"sensorId": "sensor-1", "metric": "temperature", "aggregation": "avg", "value": 20.12}
  ]
}
```

**Without dateRange:** defaults to latest 24h.  
**Empty sensorIds:** queries all sensors.

## Request Fields

**POST /api/v1/readings:**
- `sensorId` (string, required)
- `observedAt` (ISO-8601 UTC, required)
- `metrics` (object, required): `temperature`, `humidity`, `windSpeed` (at least one)

**POST /api/v1/queries/aggregate:**
- `sensorIds` (string[], optional; empty = all)
- `metrics` (string[], required): subset of `temperature`, `humidity`, `windSpeed`
- `aggregations` (string[], required): subset of `min`, `max`, `sum`, `avg`
- `dateRange` (optional): `{from, to}` (both ISO-8601, 1–31 days)

## Errors

`400` for validation failures; `5xx` for server errors.
```json
{"error": {"code": "INVALID_DATE_RANGE", "message": "..."}}
```

## Notes

- Persistence: SQLite (auto-initialized on startup)
- All timestamps: UTC, ISO-8601
- Thread-safe SQLite (`SQLITE_THREADSAFE=1`)
- Dependencies auto-fetched: cpp-httplib, nlohmann/json, SQLite3, GoogleTest