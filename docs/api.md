# OrderMatch REST API

This document defines the initial HTTP API contract for the prototype server. The API is intentionally narrow: one instrument, one matching engine, compact JSON payloads, and a thin client identity header that stays outside the matching core.

The API is not a full exchange protocol. It is a transport adapter over the internal event model described in `architecture.md`.

## Design Rules

- JSON exists only at the HTTP boundary.
- Prices and quantities are encoded as decimal strings on the wire.
- The server converts prices and quantities to integer ticks and units before queueing engine events.
- The matching core never receives HTTP objects, JSON objects, account records, or string symbols.
- Mutating requests are either rejected before the engine queue or converted into one compact inbound event.
- Successful engine processing returns an execution report, not just a flat status.

## Common Headers

Clients should include these headers where relevant:

```http
Content-Type: application/json
Accept: application/json
X-OrderMatch-Uid: 8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d
X-OrderMatch-Request-Id: 12345
```

`X-OrderMatch-Uid` is the thin client identity returned by `POST /register`. It is used for attribution, cancel ownership checks, and client-side reconciliation. It is not a balance account and does not imply authentication strength.

`X-OrderMatch-Request-Id` is an optional unsigned integer request correlation id. If absent, the server may assign one before the request enters the engine pipeline. `RequestId` does not identify an order and must not be used as an order id.

## Optional Lightweight Request Binding

For local experiments, a client may attach a lightweight request binding:

```http
Authorization: Bearer sha3_512_hex(uid || "\n" || canonical_order_payload)
```

This is deliberately not real authentication because the current sketch has no secret material. It only binds a visible uid to a canonical payload and can help catch accidental payload/header mismatches in tests.

If stronger protection is needed later, replace this with a secret-bearing scheme such as:

```http
Authorization: Bearer hmac_sha3_512_hex(client_secret, canonical_request)
```

That stronger scheme is outside V1 unless explicitly added. TLS, user passwords, custody, balances, permissions, and key rotation remain non-goals for the first version.

Canonical payload rules for the lightweight binding:

- Use the exact UTF-8 JSON request body bytes after client-side canonicalization.
- Sort object keys lexicographically.
- Do not include insignificant whitespace.
- Render decimal prices and quantities as the same strings sent in the request.
- Include only the request body for `POST /orders`; include an empty payload for `DELETE /orders/{id}` unless a future body is defined.

## Error Shape

Transport and engine errors should use a stable JSON shape:

```json
{
  "request_id": "12345",
  "result": "invalid_price",
  "message": "price is outside the configured instrument range"
}
```

`message` is for humans and may change. `result` is the stable machine-readable field.

## Result Codes

Initial result names map to the internal result-code concept:

- `ok`
- `rejected`
- `malformed_request`
- `invalid_price`
- `invalid_quantity`
- `invalid_order_type`
- `invalid_time_in_force`
- `not_found`
- `already_terminal`
- `insufficient_liquidity`
- `overloaded`

HTTP status mapping should stay centralized in the codec layer:

- `200 OK`: request processed successfully, including successful cancel and read requests.
- `201 Created`: registration created a new uid.
- `202 Accepted`: optional future mode where a request is queued but not synchronously reported.
- `400 Bad Request`: malformed JSON, invalid fields, invalid decimal formatting, invalid query parameters.
- `404 Not Found`: target order does not exist or is not visible to the uid.
- `409 Conflict`: order is already terminal or a trading rule rejects the requested transition.
- `413 Payload Too Large`: request body exceeds configured limit.
- `429 Too Many Requests`: per-uid or per-connection throttling, if added.
- `503 Service Unavailable`: engine queue overload or server not ready.

## `POST /register`

Creates a thin client identity.

The server returns an opaque UUID-like uid. The uid is used only to tag future requests and support client-side accounting. It is not a wallet, not a balance account, and not a permission model.

Request:

```http
POST /register HTTP/1.1
Content-Type: application/json
Accept: application/json
```

```json
{}
```

Response:

```http
HTTP/1.1 201 Created
Content-Type: application/json
```

```json
{
  "uid": "8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d"
}
```

V1 registration should not require email, password, account name, balances, or KYC-like data.

## `POST /orders`

Submits a limit or market order.

Request:

```http
POST /orders HTTP/1.1
Content-Type: application/json
Accept: application/json
X-OrderMatch-Uid: 8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d
X-OrderMatch-Request-Id: 12345
```

Limit order body:

```json
{
  "side": "buy",
  "type": "limit",
  "time_in_force": "gtc",
  "price": "101.25",
  "quantity": "3.5"
}
```

Market order body:

```json
{
  "side": "sell",
  "type": "market",
  "time_in_force": "ioc",
  "quantity": "2.0"
}
```

Fields:

- `side`: `buy` or `sell`.
- `type`: `limit` or `market`.
- `time_in_force`: `gtc`, `ioc`, or `fok`.
- `price`: required for limit orders, omitted for market orders.
- `quantity`: required decimal string.

Successful response:

```json
{
  "request_id": "12345",
  "result": "ok",
  "order_id": "9001",
  "status": "resting",
  "cumulative_quantity": "0",
  "leaves_quantity": "3.5",
  "sequence": "42",
  "fills": []
}
```

Response with fills:

```json
{
  "request_id": "12346",
  "result": "ok",
  "order_id": "9002",
  "status": "filled",
  "cumulative_quantity": "2.0",
  "leaves_quantity": "0",
  "sequence": "45",
  "fills": [
    {
      "maker_order_id": "8999",
      "taker_order_id": "9002",
      "price": "101.20",
      "quantity": "2.0",
      "sequence": "45"
    }
  ]
}
```

Order ids are assigned by the engine only after a submit is accepted. Rejected submits must not create resting order state.

## `DELETE /orders/{id}`

Cancels a resting order.

Request:

```http
DELETE /orders/9001 HTTP/1.1
Accept: application/json
X-OrderMatch-Uid: 8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d
X-OrderMatch-Request-Id: 12347
```

Successful response:

```json
{
  "request_id": "12347",
  "result": "ok",
  "order_id": "9001",
  "status": "cancelled",
  "cumulative_quantity": "0",
  "leaves_quantity": "0",
  "sequence": "46",
  "fills": []
}
```

Cancel ownership belongs outside the matching core. The server may reject a cancel if the uid does not own the target order, but the core should only see an order id and cancellation intent.

## `GET /book?depth=N`

Returns an aggregated depth snapshot for the single configured instrument.

Request:

```http
GET /book?depth=10 HTTP/1.1
Accept: application/json
```

Response:

```json
{
  "sequence": "46",
  "depth": 10,
  "bids": [
    {
      "price": "101.20",
      "quantity": "4.0",
      "order_count": 2
    }
  ],
  "asks": [
    {
      "price": "101.30",
      "quantity": "1.5",
      "order_count": 1
    }
  ]
}
```

Depth semantics:

- Missing `depth` uses the configured default.
- `depth=0` is invalid.
- Depth above the configured maximum is rejected.
- Each side returns at most `depth` aggregated price levels.
- The response never exposes individual resting orders or uid ownership.

## `GET /health`

Returns server readiness and coarse worker status without entering the engine queue.

Request:

```http
GET /health HTTP/1.1
Accept: application/json
```

Response:

```json
{
  "ready": true,
  "engine": "running",
  "inbound_queue": {
    "capacity": 65536,
    "available": 65500
  },
  "sequence": "46"
}
```

The exact health payload may grow, but health checks must remain transport-owned and must not mutate engine state.

