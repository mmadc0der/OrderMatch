# Thin Client Identity And Client-Side Accounting

OrderMatch V1 should avoid building a full account system. The goal is only to tag requests with a stable client uid so the server can associate accepted orders with a submitter and clients can reconcile their own local view from execution reports.

This document uses "accounting" in the narrow client-side sense: tracking submitted orders, accepted order ids, fills, cancels, rejects, and local position estimates. It does not mean balances, custody, margin, credit, deposits, withdrawals, or settlement.

## V1 Account Model

The V1 server exposes one thin registration endpoint:

```http
POST /register
```

The response is an opaque uid:

```json
{
  "uid": "8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d"
}
```

Clients include the uid on later requests:

```http
X-OrderMatch-Uid: 8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d
```

The uid is used for:

- Associating accepted orders with a submitter outside the matching core.
- Rejecting obvious cross-client cancel attempts.
- Helping the client reconcile responses and local order state.
- Supporting simple test scenarios with multiple simulated clients.

The uid is not used for:

- Funds or balance checks.
- Margin or risk.
- Authentication strength.
- Authorization roles.
- Identity verification.
- Matching priority.
- Core resting order representation.

## Boundary Rule

Client identity must stop at the engine-side ownership registry. It must not enter the matching core.

The matching core should continue to operate on:

- `OrderId`
- `PriceTicks`
- `QuantityUnits`
- `SequenceNumber`
- compact order flags

The engine or server may maintain a side map:

```text
OrderId -> Uid
```

That map is used for ownership checks and reporting. It is not part of price-time priority and must not affect matching behavior.

## Request Binding Option

A lightweight request-binding header may be used in experiments:

```http
Authorization: Bearer sha3_512_hex(uid || "\n" || canonical_order_payload)
```

This is only a payload binding check. It is not secure authentication because there is no secret. Anyone who knows the uid and payload can compute the same value.

The useful V1 property is modest:

- It catches accidental uid/payload mismatch in clients and tests.
- It gives the server a stable signature-like string for logging and diagnostics.
- It avoids introducing account passwords, sessions, API keys, or key storage.

If the project later needs actual request authentication, the concept should become secret-bearing:

```http
Authorization: Bearer hmac_sha3_512_hex(client_secret, canonical_request)
```

That would require a new registration response, secret storage rules, rotation, replay protection, and TLS assumptions. Those are intentionally outside the thin V1 model.

## Client-Side Ledger

The client should keep a local append-only ledger of its own requests and reports. The server remains the source of truth for matching state, but the client can derive a useful local view from responses.

Recommended client ledger events:

- `registered`: uid created or loaded.
- `submit_sent`: local order intent sent with request id.
- `submit_rejected`: server rejected before or during engine processing.
- `order_accepted`: engine assigned order id.
- `fill_received`: execution report included one or more fills.
- `order_resting`: accepted order has leaves quantity.
- `cancel_sent`: cancel request sent for an order id.
- `cancelled`: cancel confirmed.
- `cancel_rejected`: cancel failed because the order was missing, terminal, or not owned by the uid.

The client can maintain these local projections:

- Open orders by `OrderId`.
- Request correlation by `RequestId`.
- Cumulative filled quantity per order.
- Leaves quantity per order.
- Approximate net position for the single instrument.
- Last seen engine `SequenceNumber`.

The local position estimate is only as complete as the reports the client has observed. V1 does not need a server-side balance ledger.

## Minimal Client State

A simple client state file can be enough:

```json
{
  "uid": "8f2ec3a9-7f3a-4a68-8725-5f6d067fb51d",
  "next_request_id": "12348",
  "last_sequence": "46",
  "open_orders": {
    "9001": {
      "side": "buy",
      "price": "101.25",
      "original_quantity": "3.5",
      "cumulative_quantity": "0",
      "leaves_quantity": "3.5"
    }
  }
}
```

This state is a convenience for the client. The server should not trust it.

## Cancel Ownership

Cancel ownership should be checked outside the core:

1. HTTP layer extracts uid from `X-OrderMatch-Uid`.
2. Decode layer maps `DELETE /orders/{id}` to a cancel intent.
3. Engine/server ownership registry checks whether `OrderId` belongs to uid.
4. If ownership passes, the engine asks the core to cancel the order id.
5. The core cancels by order id only and does not see uid.

For privacy and simplicity, a cancel for another uid's order can return the same result as an unknown order:

```json
{
  "result": "not_found"
}
```

That avoids exposing whether another client has a specific order id.

## Reconciliation

The client should treat execution reports as the authoritative stream for its own local order state.

Important reconciliation rules:

- `OrderId` appears only after accepted submit.
- Rejected submit does not create an open order.
- Each fill reduces leaves quantity.
- A filled order has zero leaves quantity.
- A cancelled order has zero leaves quantity after cancel confirmation.
- `SequenceNumber` lets the client detect whether reports were observed in engine order.
- `RequestId` correlates transport requests and responses, but it is not an order id.

If the client loses local state, it can continue with the same uid but may not be able to reconstruct all open orders unless a future `GET /orders` endpoint is added. V1 can avoid that endpoint if tests and demos operate synchronously from execution reports.

## Explicit Non-Goals

V1 client identity does not include:

- Deposits, withdrawals, or balances.
- Credit limits or margin.
- Settlement.
- Multi-user permissions.
- Password login.
- API key lifecycle.
- Replay protection.
- Regulatory identity.
- Durable server-side account recovery.

These can be added later as separate layers. They should not be smuggled into `Order`, `Trade`, or price-level data structures.

